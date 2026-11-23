// Builds the ArkModAPI table and loads mods from the ArkMods folder.
#include "mods.h"

#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <algorithm>
#include <set>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dispatch.h"
#include "engine.h"
#include "hook.h"
#include "log.h"

namespace mods {
namespace {

using engine::UObject;
std::string gModsDir;

UObject* U(::UObject* o) { return reinterpret_cast<UObject*>(o); }
::UObject* C(UObject* o) { return reinterpret_cast<::UObject*>(o); }

int CopyOut(const std::string& s, char* buf, int n) {
    if (!buf || n <= 0) return static_cast<int>(s.size());
    int len = std::min<int>(static_cast<int>(s.size()), n - 1);
    std::memcpy(buf, s.data(), len);
    buf[len] = 0;
    return len;
}

void ApiLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    logx::WriteV(4, "mod", fmt, ap);
    va_end(ap);
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
        return dispatch::AddHook(p, pre, post, u);
    };
    a.run_on_game_thread = [](ArkTask t, void* u) { dispatch::Post(t, u); };
    a.on_tick = [](ArkTask t, void* u) { dispatch::AddTick(t, u); };
    a.inline_hook = [](void* target, void* repl) -> void* {
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
    return a;
}

ArkModAPI gApi;

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

}  // namespace

void SetModsDir(const std::string& dir) { gModsDir = dir; }

int LoadNew(const std::string& cacheDir) {
    static std::set<std::string> seen;  // file names already attempted (loaded or failed)
    if (seen.empty()) {
        gApi = MakeApi();
        mkdir(cacheDir.c_str(), 0700);
    }

    std::vector<std::string> files;
    if (DIR* d = opendir(gModsDir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 3 && n.compare(n.size() - 3, 3, ".so") == 0 && !seen.count(n)) files.push_back(n);
        }
        closedir(d);
    } else {
        static bool warned = false;
        if (!warned) LOGE("cannot open mods dir %s", gModsDir.c_str());
        warned = true;
        return 0;
    }
    if (files.empty()) return 0;
    std::sort(files.begin(), files.end());
    LOG("found %zu new mod(s) in %s", files.size(), gModsDir.c_str());

    int loaded = 0;
    for (const std::string& f : files) {
        seen.insert(f);
        // Libraries can't be mapped executable from /sdcard: copy to app-private storage first.
        std::string src = gModsDir + "/" + f, dst = cacheDir + "/" + f;
        if (!CopyFile(src, dst)) { LOGE("%s: copy failed", f.c_str()); continue; }
        void* h = dlopen(dst.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) { LOGE("%s: dlopen failed: %s", f.c_str(), dlerror()); continue; }
        auto init = reinterpret_cast<int (*)(const ArkModAPI*)>(dlsym(h, "ArkMod_Init"));
        auto name = reinterpret_cast<const char* (*)()>(dlsym(h, "ArkMod_Name"));
        if (!init) { LOGE("%s: no ArkMod_Init export", f.c_str()); continue; }
        const char* display = name ? name() : f.c_str();
        int rc = init(&gApi);
        if (rc == 0) { ++loaded; LOG("loaded mod '%s' (%s)", display, f.c_str()); }
        else LOGE("mod '%s' init returned %d", display, rc);
    }
    return loaded;
}

}  // namespace mods
