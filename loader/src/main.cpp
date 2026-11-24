// ArkModLoader entry point: wait for libUE4, hook ProcessEvent, wait for the engine, load mods.
// Loaded either as a DT_NEEDED of the game's libgnustl_shared.so (PC installer, MuMu) or by the
// Zygisk module of the Magisk package (phone) right after the app process is specialized.
#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "dispatch.h"
#include "elfsym.h"
#include "engine.h"
#include "log.h"
#include "mods.h"
#include "ui.h"

namespace {

constexpr const char* kVersion = "0.7.0";

std::string PackageName() {
    char buf[256] = {};
    if (FILE* f = fopen("/proc/self/cmdline", "r")) {
        fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
    }
    return buf;
}

bool DirWritable(const std::string& d) {
    mkdir(d.c_str(), 0770);
    return access(d.c_str(), W_OK) == 0;
}

void SelfTest() {
    using namespace engine;
    engine::UObject* tb = FindObject("/Script/UMG.TextBlock");
    engine::UObject* setText = FindField(tb, "SetText");
    LOG("selftest: TextBlock.Text offset=%d, SetText params=%d (expect 24), world=%s",
        PropertyOffset(tb, "Text"), setText ? ParamsSize(setText) : -1, Name(World()).c_str());
}

std::string ReadFile(const std::string& path) {
    std::string out;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        char buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
        fclose(f);
    }
    return out;
}

bool WriteFile(const std::string& path, const std::string& data) {
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok &= fclose(f) == 0;
    return ok && rename(tmp.c_str(), path.c_str()) == 0;
}

// The Magisk module drops its bundled mods into <app files>/arkml_bundle (with a VERSION file).
// Copy them into the mods folder once per module version, so mods the user deleted stay deleted
// until the next module update (the PC installer behaves the same way).
void SeedBundledMods(const std::string& bundleDir, const std::string& modsDir) {
    std::string ver = ReadFile(bundleDir + "/VERSION");
    if (ver.empty()) return;
    const std::string marker = modsDir + "/.bundled_version";
    if (ReadFile(marker) == ver) return;
    int copied = 0;
    if (DIR* d = opendir(bundleDir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n[0] == '.' || n == "VERSION") continue;
            std::string data = ReadFile(bundleDir + "/" + n);
            if (!data.empty() && WriteFile(modsDir + "/" + n, data)) ++copied;
            else LOGE("bundled mod %s: copy failed", n.c_str());
        }
        closedir(d);
    }
    WriteFile(marker, ver);
    LOG("installed %d bundled mod file(s) from the Magisk module %s", copied, ver.c_str());
}

// libUE4 is loaded by the game's Java code. In the game's own linker namespace dlopen finds it;
// from the Zygisk-injected copy only dl_iterate_phdr can see it.
bool WaitForUE4(void** handle, elfsym::Image* image) {
    for (int i = 0;; ++i) {
#ifndef ARKML_FORCE_ELFSYM  // test switch: exercise the Zygisk lookup path on the emulator
        if ((*handle = dlopen("libUE4.so", RTLD_NOW | RTLD_NOLOAD))) return true;
#endif
        if (elfsym::Find("/libUE4.so", image)) {
            usleep(500 * 1000);  // let the linker finish relocating it
            elfsym::Find("/libUE4.so", image);
            if (elfsym::Lookup(*image, "GUObjectArray")) {
                LOG("libUE4.so found via dl_iterate_phdr (separate linker namespace)");
                return true;
            }
        }
        usleep(100 * 1000);
        if (i == 600) LOG("still waiting for libUE4.so...");
        if (i > 18000) return false;  // 30 minutes
    }
}

void* LoaderThread(void*) {
    void* ue4 = nullptr;
    elfsym::Image image;
    if (!WaitForUE4(&ue4, &image)) { LOGE("libUE4.so never loaded"); return nullptr; }
    if (!engine::Init(ue4, ue4 ? nullptr : &image)) { LOGE("engine symbols missing, loader disabled"); return nullptr; }
    if (!dispatch::Install(engine::Sym("_ZN7UObject12ProcessEventEP9UFunctionPv"))) return nullptr;
    LOG("ProcessEvent hooked");

    const std::string pkg = PackageName();
    std::string modsDir = "/sdcard/Android/data/" + pkg + "/files/ArkMods";
    if (!DirWritable("/sdcard/Android/data/" + pkg + "/files") || !DirWritable(modsDir))
        modsDir = "/data/data/" + pkg + "/files/ArkMods", DirWritable(modsDir);
    logx::OpenFile((modsDir + "/loader.log").c_str());
    LOG("ArkModLoader %s | pkg %s | mods dir %s", kVersion, pkg.c_str(), modsDir.c_str());
    SeedBundledMods("/data/data/" + pkg + "/files/arkml_bundle", modsDir);

    while (!engine::Ready() || !engine::World()) usleep(250 * 1000);
    LOG("engine ready: %d objects, world %s", engine::Objects()->ObjObjects.NumElements,
        engine::Name(engine::World()).c_str());
    SelfTest();

    if (!ui::Init()) LOGE("in-game MODS panel unavailable (mods still load)");
    mods::Setup(modsDir, "/data/data/" + pkg + "/files/arkmods_cache");
    LOG("watching %s (new, changed and removed mods are applied live)", modsDir.c_str());

    for (int t = 1;; ++t) {
        mods::Scan();
        sleep(3);
        if (t % 20 == 0)
            LOG("alive: world=%s ProcessEvent calls=%llu", engine::Name(engine::World()).c_str(),
                static_cast<unsigned long long>(dispatch::CallCount()));
    }
}

}  // namespace

__attribute__((constructor)) static void OnLoad() {
    LOG("libarkmodloader.so loaded (pid %d)", getpid());
    pthread_t t;
    pthread_create(&t, nullptr, LoaderThread, nullptr);
    pthread_detach(t);
}
