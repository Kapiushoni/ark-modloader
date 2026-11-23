#include "mods.h"

#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

#include "dispatch.h"
#include "engine.h"
#include "hook.h"
#include "log.h"
#include "ui.h"

namespace mods {
namespace {

using engine::UObject;

std::string gModsDir, gCacheDir;

// ------------------------------------------------------------------ per-mod settings file
struct Config {
    std::string path;
    time_t mtime = 0;
    off_t size = -1;
    double lastCheck = -1;
    std::map<std::string, std::string> kv;
};

double Now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::map<std::string, std::string> ReadIni(const std::string& path) {
    std::map<std::string, std::string> kv;
    if (FILE* f = fopen(path.c_str(), "r")) {
        char line[1024];
        while (fgets(line, sizeof line, f)) {
            std::string l = Trim(line);
            if (l.empty() || l[0] == '#' || l[0] == ';') continue;
            size_t eq = l.find('=');
            if (eq != std::string::npos) kv[Trim(l.substr(0, eq))] = Trim(l.substr(eq + 1));
        }
        fclose(f);
    }
    return kv;
}

void Refresh(Config& c) {
    double now = Now();
    if (now - c.lastCheck < 1.0) return;
    c.lastCheck = now;
    struct stat st{};
    bool exists = stat(c.path.c_str(), &st) == 0;
    time_t mt = exists ? st.st_mtime : 0;
    off_t sz = exists ? st.st_size : -1;
    if (mt == c.mtime && sz == c.size) return;
    c.mtime = mt;
    c.size = sz;
    c.kv = ReadIni(c.path);
}

// ------------------------------------------------------------------ registry
struct Mod {
    std::string file;              // libfoo.so
    std::string id;                // foo
    std::string name;              // ArkMod_Name() or id
    bool enabled = true;           // user's choice
    bool loaded = false;
    bool pending = false;          // an operation is queued on the game thread
    bool usesInlineHooks = false;  // cannot be unloaded at runtime
    std::string note;
    time_t mtime = 0;              // of the file version that is loaded / failed
    off_t size = 0;
    int generation = 0;
    const void* base = nullptr;    // module base while loaded (= owner of its registrations)
    void (*shutdown)() = nullptr;
    Config cfg;
};

std::recursive_mutex gMu;
std::vector<std::unique_ptr<Mod>> gMods;

Mod* ByFile(const std::string& file) {
    for (auto& m : gMods) if (m->file == file) return m.get();
    return nullptr;
}
Mod* ByBase(const void* base) {
    if (!base) return nullptr;
    for (auto& m : gMods) if (m->loaded && m->base == base) return m.get();
    return nullptr;
}

const void* OwnerOf(const void* addr) {
    Dl_info info{};
    return dladdr(addr, &info) ? info.dli_fbase : nullptr;
}

std::string IdOf(const std::string& file) {
    std::string id = file.substr(0, file.size() - 3);  // drop .so
    if (id.compare(0, 3, "lib") == 0) id = id.substr(3);
    return id;
}

// ------------------------------------------------------------------ mods.ini (enable state)
std::string EnablePath() { return gModsDir + "/mods.ini"; }

void SaveEnabled() {
    std::map<std::string, std::string> kv = ReadIni(EnablePath());
    for (auto& m : gMods) kv[m->file] = m->enabled ? "1" : "0";
    if (FILE* f = fopen(EnablePath().c_str(), "w")) {
        fputs("# ArkModLoader: 1 = mod enabled, 0 = disabled (also switchable in the in-game MODS panel)\n", f);
        for (auto& [k, v] : kv) fprintf(f, "%s = %s\n", k.c_str(), v.c_str());
        fclose(f);
    }
}

// ------------------------------------------------------------------ API attribution
// Functions below are called from mod code; the caller's module identifies the mod.
#define CALLER_MOD() ByBase(OwnerOf(__builtin_return_address(0)))

const char* ConfigLookup(Mod* m, const char* key, const std::string& def) {
    static const char* kEmpty = "";
    if (!m || !key) return kEmpty;
    Refresh(m->cfg);
    auto it = m->cfg.kv.find(key);
    if (it != m->cfg.kv.end()) return it->second.c_str();
    // Unknown key: append it with its default so the user can discover and edit it.
    if (FILE* f = fopen(m->cfg.path.c_str(), "a")) {
        if (m->cfg.size <= 0) fprintf(f, "# Settings for %s (edit while the game runs)\n", m->name.c_str());
        fprintf(f, "%s = %s\n", key, def.c_str());
        fclose(f);
    }
    m->cfg.lastCheck = -1;  // pick up the new file state next call
    return m->cfg.kv.emplace(key, def).first->second.c_str();
}

__attribute__((noinline)) const char* ApiConfigString(const char* key, const char* def) {
    std::lock_guard<std::recursive_mutex> l(gMu);
    return ConfigLookup(CALLER_MOD(), key, def ? def : "");
}
__attribute__((noinline)) int ApiConfigInt(const char* key, int def) {
    std::lock_guard<std::recursive_mutex> l(gMu);
    const char* v = ConfigLookup(CALLER_MOD(), key, std::to_string(def));
    char* end;
    long r = strtol(v, &end, 10);
    return end != v ? int(r) : def;
}
__attribute__((noinline)) float ApiConfigFloat(const char* key, float def) {
    std::lock_guard<std::recursive_mutex> l(gMu);
    char buf[32];
    snprintf(buf, sizeof buf, "%g", def);
    const char* v = ConfigLookup(CALLER_MOD(), key, buf);
    char* end;
    float r = strtof(v, &end);
    return end != v ? r : def;
}
__attribute__((noinline)) void ApiUiToggle(const char* label, int* value) {
    ui::AddToggle(label, value, OwnerOf(__builtin_return_address(0)));
}

void ApiLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    logx::WriteV(4, "mod", fmt, ap);
    va_end(ap);
}

UObject* U(::UObject* o) { return reinterpret_cast<UObject*>(o); }
::UObject* C(UObject* o) { return reinterpret_cast<::UObject*>(o); }

int CopyOut(const std::string& s, char* buf, int n) {
    if (!buf || n <= 0) return static_cast<int>(s.size());
    int len = std::min<int>(static_cast<int>(s.size()), n - 1);
    std::memcpy(buf, s.data(), len);
    buf[len] = 0;
    return len;
}

ArkModAPI MakeApi() {
    ArkModAPI a{};
    a.version = ARKMOD_API_VERSION;
    a.mods_dir = gModsDir.c_str();
    a.log = ApiLog;
    a.find_symbol = [](const char* n) { return engine::Sym(n); };
    a.find_object = [](const char* p) { return C(engine::FindObject(p)); };
    a.object_name = [](::UObject* o, char* b, int n) { return CopyOut(engine::Name(U(o)), b, n); };
    a.object_path = [](::UObject* o, char* b, int n) { return CopyOut(engine::Path(U(o)), b, n); };
    a.object_class = [](::UObject* o) { return o ? C(reinterpret_cast<UObject*>(U(o)->ClassPrivate)) : nullptr; };
    a.is_a = [](::UObject* o, ::UObject* c) { return engine::IsA(U(o), U(c)) ? 1 : 0; };
    a.for_each_object = [](::UObject* cls, ArkObjectVisitor cb, void* user) {
        auto& arr = engine::Objects()->ObjObjects;
        int visited = 0;
        for (int i = 0; i < arr.NumElements; ++i) {
            UObject* o = arr.Objects[i].Object;
            if (!o || engine::IsCDO(o) || (cls && !engine::IsA(o, U(cls)))) continue;
            ++visited;
            if (!cb(C(o), user)) break;
        }
        return visited;
    };
    a.get_world = [] { return C(engine::World()); };
    a.property_offset = [](::UObject* s, const char* n) { return engine::PropertyOffset(U(s), n); };
    a.find_function = [](::UObject* s, const char* n) { return C(engine::FindField(U(s), n)); };
    a.params_size = [](::UObject* f) { return engine::ParamsSize(U(f)); };
    a.process_event = [](::UObject* o, ::UObject* f, void* p) { dispatch::CallOriginal(U(o), U(f), p); };
    a.hook_function = [](const char* p, ArkPreHook pre, ArkPostHook post, void* u) {
        return dispatch::AddHook(p, pre, post, u, OwnerOf(pre ? reinterpret_cast<void*>(pre) : reinterpret_cast<void*>(post)));
    };
    a.run_on_game_thread = [](ArkTask t, void* u) { dispatch::Post(t, u, OwnerOf(reinterpret_cast<void*>(t))); };
    a.on_tick = [](ArkTask t, void* u) { dispatch::AddTick(t, u, OwnerOf(reinterpret_cast<void*>(t))); };
    a.inline_hook = [](void* target, void* repl) -> void* {
        {
            std::lock_guard<std::recursive_mutex> l(gMu);
            if (Mod* m = ByBase(OwnerOf(repl))) m->usesInlineHooks = true;
        }
        const char* err = "?";
        void* orig = hook::InlineHook(target, repl, &err);
        if (!orig) LOGE("inline_hook(%p) failed: %s", target, err);
        return orig;
    };
    a.make_text = [](const char* s, ArkText* out) {
        engine::FText t = engine::MakeText(s);
        std::memcpy(out, &t, sizeof t);
    };
    a.text_to_utf8 = [](const ArkText* t, char* b, int n) {
        return CopyOut(engine::TextToUtf8(*reinterpret_cast<const engine::FText*>(t)), b, n);
    };
    a.exec_command = [](const char* cmd) { return engine::ExecCommand(cmd) ? 1 : 0; };
    // v2
    a.config_string = ApiConfigString;
    a.config_int = ApiConfigInt;
    a.config_float = ApiConfigFloat;
    a.ui_toggle = ApiUiToggle;
    a.on_draw = [](ArkDrawFn fn, void* u) { ui::AddDraw(fn, u, OwnerOf(reinterpret_cast<void*>(fn))); };
    a.draw_rect = ui::DrawRect;
    a.draw_text = ui::DrawText;
    a.on_touch = [](ArkTouchFn fn, void* u) { ui::AddTouch(fn, u, OwnerOf(reinterpret_cast<void*>(fn))); };
    return a;
}

ArkModAPI gApi;

// ------------------------------------------------------------------ lifecycle (game thread)
bool CopyFile(const std::string& from, const std::string& to) {
    FILE* in = fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    char buf[64 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    chmod(to.c_str(), 0700);
    return true;
}

void Load(Mod& m) {
    struct stat st{};
    std::string src = gModsDir + "/" + m.file;
    if (stat(src.c_str(), &st) != 0) return;
    m.mtime = st.st_mtime;
    m.size = st.st_size;
    m.note.clear();

    // Copy to app-private storage (no exec from /sdcard) under a new name per generation,
    // so a changed file is really loaded again instead of returning the cached handle.
    // The counter is process-wide: a cache path must never be reused while an older copy
    // (never dlclose'd) is still mapped, or we would overwrite live code.
    static int sCopies = 0;
    ++m.generation;
    std::string dst = gCacheDir + "/" + m.id + "." + std::to_string(++sCopies) + ".so";
    if (!CopyFile(src, dst)) { m.note = "copy failed"; LOGE("%s: copy failed", m.file.c_str()); return; }
    void* h = dlopen(dst.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) { m.note = "failed to load - see loader.log"; LOGE("%s: dlopen failed: %s", m.file.c_str(), dlerror()); return; }
    auto init = reinterpret_cast<int (*)(const ArkModAPI*)>(dlsym(h, "ArkMod_Init"));
    auto name = reinterpret_cast<const char* (*)()>(dlsym(h, "ArkMod_Name"));
    m.shutdown = reinterpret_cast<void (*)()>(dlsym(h, "ArkMod_Shutdown"));
    if (!init) { m.note = "not a mod (no ArkMod_Init)"; LOGE("%s: no ArkMod_Init export", m.file.c_str()); return; }
    m.name = name ? name() : m.id;
    m.base = OwnerOf(reinterpret_cast<void*>(init));
    m.loaded = true;  // before Init, so config/toggle calls made from Init resolve to this mod
    m.usesInlineHooks = false;
    int rc = init(&gApi);
    if (rc != 0) {
        dispatch::RemoveOwner(m.base);
        ui::RemoveOwner(m.base);
        m.loaded = false;
        m.note = "init failed (" + std::to_string(rc) + ")";
        LOGE("mod '%s' init returned %d", m.name.c_str(), rc);
        return;
    }
    LOG("loaded mod '%s' (%s, gen %d)%s", m.name.c_str(), m.file.c_str(), m.generation,
        m.usesInlineHooks ? " [uses inline hooks: restart needed to unload]" : "");
}

// Returns false if the mod cannot be unloaded at runtime.
bool Unload(Mod& m) {
    if (!m.loaded) return true;
    if (m.usesInlineHooks) return false;
    if (m.shutdown) m.shutdown();
    dispatch::RemoveOwner(m.base);
    ui::RemoveOwner(m.base);
    m.loaded = false;
    m.base = nullptr;
    // The library stays mapped (no dlclose): a callback might still be on another thread's
    // stack. The next load uses a fresh copy, so this only costs a little memory.
    LOG("unloaded mod '%s'", m.name.c_str());
    return true;
}

enum class Op { Load, Reload, Unload, Remove };
struct Request { std::string file; Op op; };

void RunRequest(void* p) {
    std::unique_ptr<Request> r(static_cast<Request*>(p));
    std::lock_guard<std::recursive_mutex> l(gMu);
    Mod* m = ByFile(r->file);
    if (!m) return;
    m->pending = false;
    switch (r->op) {
    case Op::Load:
        if (!m->loaded && m->enabled) Load(*m);
        break;
    case Op::Reload:
        if (Unload(*m)) {
            LOG("reloading '%s' (file changed)", m->name.c_str());
            Load(*m);
        } else {
            struct stat st{};
            stat((gModsDir + "/" + m->file).c_str(), &st);
            m->mtime = st.st_mtime;  // don't retry every scan
            m->size = st.st_size;
            m->note = "updated - restart the game to apply";
            LOG("'%s' changed but uses inline hooks: restart the game to apply", m->name.c_str());
        }
        break;
    case Op::Unload:
    case Op::Remove:
        if (!Unload(*m)) m->note = "restart the game to apply";
        if (r->op == Op::Remove && !m->loaded)
            gMods.erase(std::remove_if(gMods.begin(), gMods.end(), [&](auto& x) { return x.get() == m; }), gMods.end());
        break;
    }
}

void Queue(Mod& m, Op op) {
    if (m.pending) return;
    m.pending = true;
    dispatch::Post(RunRequest, new Request{m.file, op}, nullptr);
}

}  // namespace

void Setup(const std::string& modsDir, const std::string& cacheDir) {
    gModsDir = modsDir;
    gCacheDir = cacheDir;
    mkdir(cacheDir.c_str(), 0700);
    // Copies from previous runs are not mapped in this process: safe to delete.
    if (DIR* d = opendir(cacheDir.c_str())) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') unlink((cacheDir + "/" + e->d_name).c_str());
        closedir(d);
    }
    gApi = MakeApi();
}

void Scan() {
    std::map<std::string, struct stat> files;
    if (DIR* d = opendir(gModsDir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            struct stat st{};
            if (n.size() > 3 && n.compare(n.size() - 3, 3, ".so") == 0 && stat((gModsDir + "/" + n).c_str(), &st) == 0)
                files[n] = st;
        }
        closedir(d);
    }
    std::map<std::string, std::string> enabledIni = ReadIni(EnablePath());

    std::lock_guard<std::recursive_mutex> l(gMu);
    bool newMods = false;
    for (auto& [file, st] : files) {
        Mod* m = ByFile(file);
        if (!m) {
            auto nm = std::make_unique<Mod>();
            nm->file = file;
            nm->id = IdOf(file);
            nm->name = nm->id;
            auto it = enabledIni.find(file);
            nm->enabled = it == enabledIni.end() || it->second != "0";
            nm->cfg.path = gModsDir + "/" + nm->id + ".ini";
            m = nm.get();
            gMods.push_back(std::move(nm));
            newMods = true;
            LOG("found mod %s (%s)", file.c_str(), m->enabled ? "enabled" : "disabled");
            if (m->enabled) Queue(*m, Op::Load);
            continue;
        }
        bool changed = st.st_mtime != m->mtime || st.st_size != m->size;
        if (changed && m->loaded) Queue(*m, Op::Reload);
        else if (changed && m->enabled && !m->loaded && m->mtime != 0) Queue(*m, Op::Load);  // retry a fixed mod
    }
    for (auto& m : gMods)
        if (!files.count(m->file)) Queue(*m, Op::Remove);
    if (newMods) SaveEnabled();
}

void SetEnabled(const std::string& file, bool enabled) {
    std::lock_guard<std::recursive_mutex> l(gMu);
    Mod* m = ByFile(file);
    if (!m) return;
    m->enabled = enabled;
    SaveEnabled();
    LOG("mod '%s' %s by user", m->name.c_str(), enabled ? "enabled" : "disabled");
    if (enabled) {
        if (m->loaded) m->note.clear();  // was "restart to apply" after a refused unload
        else Load(*m);
    } else if (!Unload(*m)) {
        m->note = "restart the game to apply";
    }
}

std::vector<ui::ModRow> PanelRows() {
    std::lock_guard<std::recursive_mutex> l(gMu);
    std::vector<ui::ModRow> rows;
    for (auto& m : gMods) rows.push_back({m->file, m->name, m->enabled, m->loaded, m->note, m->base});
    return rows;
}

}  // namespace mods
