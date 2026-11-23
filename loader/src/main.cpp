// ArkModLoader entry point: wait for libUE4, hook ProcessEvent, wait for the engine, load mods.
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "dispatch.h"
#include "engine.h"
#include "log.h"
#include "mods.h"
#include "ui.h"

namespace {

constexpr const char* kVersion = "0.4.0";

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

void* LoaderThread(void*) {
    void* ue4 = nullptr;
    for (int i = 0; !ue4; ++i) {
        ue4 = dlopen("libUE4.so", RTLD_NOW | RTLD_NOLOAD);
        if (!ue4) usleep(50 * 1000);
        if (i > 1200) { LOGE("libUE4.so never loaded"); return nullptr; }
    }
    if (!engine::Init(ue4)) { LOGE("engine symbols missing, loader disabled"); return nullptr; }
    if (!dispatch::Install(engine::Sym("_ZN7UObject12ProcessEventEP9UFunctionPv"))) return nullptr;
    LOG("ProcessEvent hooked");

    const std::string pkg = PackageName();
    std::string modsDir = "/sdcard/Android/data/" + pkg + "/files/ArkMods";
    if (!DirWritable("/sdcard/Android/data/" + pkg + "/files") || !DirWritable(modsDir))
        modsDir = "/data/data/" + pkg + "/files/ArkMods", DirWritable(modsDir);
    logx::OpenFile((modsDir + "/loader.log").c_str());
    LOG("ArkModLoader %s | pkg %s | mods dir %s", kVersion, pkg.c_str(), modsDir.c_str());

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
