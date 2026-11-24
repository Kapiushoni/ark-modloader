// ArkModLoader Zygisk module: loads libarkmodloader.so into ARK Mobile, nothing else.
//
// preAppSpecialize (zygote privileges, module dir readable):
//   other apps -> ask Zygisk to unload us, done;
//   the game   -> open the loader, read it and the bundled mods into memory.
// postAppSpecialize (app sandbox):
//   stage the bundled mods in <app>/files/arkml_bundle (the loader copies them into ArkMods),
//   then load the loader: from the module file's fd, else from a memfd, else from a private copy.
// The game's own files are never modified.
#include <android/dlext.h>
#include <android/log.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "zygisk.hpp"

#define TAG "ArkModLoader-zygisk"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace {

constexpr const char* kPackage = "com.studiowildcard.wardrumstudios.ark";
constexpr const char* kLoaderPath = "loader/libarkmodloader.so";
constexpr const char* kLoaderName = "libarkmodloader.so";
constexpr int kMaxBundle = 32;

struct Blob {
    char name[128] = {};
    char* data = nullptr;
    size_t size = 0;
};

bool ReadFd(int fd, Blob* b) {
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > (64 << 20)) return false;
    b->data = static_cast<char*>(malloc(st.st_size));
    if (!b->data) return false;
    size_t got = 0;
    while (got < static_cast<size_t>(st.st_size)) {
        ssize_t n = pread(fd, b->data + got, st.st_size - got, got);
        if (n <= 0) break;
        got += n;
    }
    if (got != static_cast<size_t>(st.st_size)) { free(b->data); b->data = nullptr; return false; }
    b->size = got;
    return true;
}

bool ReadAt(int dirfd, const char* path, Blob* b) {
    int fd = openat(dirfd, path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    bool ok = ReadFd(fd, b);
    close(fd);
    return ok;
}

bool WriteAll(const char* path, const char* data, size_t size) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, data + done, size - done);
        if (n <= 0) break;
        done += n;
    }
    close(fd);
    if (done != size || rename(tmp, path) != 0) { unlink(tmp); return false; }
    return true;
}

bool SameContent(const char* path, const Blob& b) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    Blob cur;
    bool same = ReadFd(fd, &cur) && cur.size == b.size && memcmp(cur.data, b.data, b.size) == 0;
    close(fd);
    free(cur.data);
    return same;
}

void* DlopenFd(int fd) {
    android_dlextinfo ext{};
    ext.flags = ANDROID_DLEXT_USE_LIBRARY_FD;
    ext.library_fd = fd;
    return android_dlopen_ext(kLoaderName, RTLD_NOW, &ext);
}

class ArkModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if (!IsGame(args)) { api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY); return; }
        if (args->app_data_dir) {
            const char* d = env_->GetStringUTFChars(args->app_data_dir, nullptr);
            if (d) snprintf(dataDir_, sizeof dataDir_, "%s", d), env_->ReleaseStringUTFChars(args->app_data_dir, d);
        }
        int dir = api_->getModuleDir();
        if (dir < 0) { LOGE("getModuleDir failed"); return; }

        loaderFd_ = openat(dir, kLoaderPath, O_RDONLY | O_CLOEXEC);
        if (loaderFd_ >= 0 && !api_->exemptFd(loaderFd_)) { close(loaderFd_); loaderFd_ = -1; }
        if (!ReadAt(dir, kLoaderPath, &loader_)) LOGE("cannot read %s from the module", kLoaderPath);

        ReadAt(dir, "VERSION", &version_);
        int mods = openat(dir, "mods", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (DIR* d = mods >= 0 ? fdopendir(mods) : nullptr) {
            while (dirent* e = readdir(d)) {
                if (e->d_name[0] == '.' || bundleCount_ >= kMaxBundle) continue;
                Blob& b = bundle_[bundleCount_];
                snprintf(b.name, sizeof b.name, "%s", e->d_name);
                if (ReadAt(mods, e->d_name, &b)) ++bundleCount_;
            }
            closedir(d);  // also closes `mods`
        } else if (mods >= 0) {
            close(mods);
        }
        close(dir);
        target_ = true;
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        // The loader is a separate library, so this module can always be unloaded afterwards.
        api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        if (!target_) return;
        StageBundle();
        LoadLoader();
        for (int i = 0; i < bundleCount_; ++i) free(bundle_[i].data);
        free(version_.data);
        free(loader_.data);
        if (loaderFd_ >= 0) close(loaderFd_);
    }

private:
    bool IsGame(zygisk::AppSpecializeArgs* args) {
        if (!args->nice_name) return false;
        const char* n = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!n) return false;
        bool game = strcmp(n, kPackage) == 0;
        env_->ReleaseStringUTFChars(args->nice_name, n);
        return game;
    }

    void StageBundle() {
        if (!dataDir_[0] || !version_.data) return;
        char dir[512], path[700];
        snprintf(dir, sizeof dir, "%s/files", dataDir_);
        mkdir(dir, 0771);
        snprintf(dir, sizeof dir, "%s/files/arkml_bundle", dataDir_);
        mkdir(dir, 0700);
        snprintf(path, sizeof path, "%s/VERSION", dir);
        if (SameContent(path, version_)) return;  // already staged for this module version
        // Old files first, so a mod dropped from the bundle isn't re-installed.
        if (DIR* d = opendir(dir)) {
            while (dirent* e = readdir(d))
                if (e->d_name[0] != '.') snprintf(path, sizeof path, "%s/%s", dir, e->d_name), unlink(path);
            closedir(d);
        }
        for (int i = 0; i < bundleCount_; ++i) {
            snprintf(path, sizeof path, "%s/%s", dir, bundle_[i].name);
            if (!WriteAll(path, bundle_[i].data, bundle_[i].size)) LOGE("stage %s failed", bundle_[i].name);
        }
        snprintf(path, sizeof path, "%s/VERSION", dir);
        WriteAll(path, version_.data, version_.size);  // last: marks the bundle complete
        LOGI("staged %d bundled mod file(s)", bundleCount_);
    }

    void LoadLoader() {
        if (loaderFd_ >= 0) {
            if (DlopenFd(loaderFd_)) { LOGI("loader loaded from the module"); return; }
            LOGE("module fd: %s", dlerror());
        }
        if (!loader_.data) { LOGE("loader not available, game runs unmodded"); return; }

        int mfd = static_cast<int>(syscall(__NR_memfd_create, kLoaderName, 1u /* MFD_CLOEXEC */));
        if (mfd >= 0) {
            bool ok = static_cast<size_t>(write(mfd, loader_.data, loader_.size)) == loader_.size;
            void* h = ok ? DlopenFd(mfd) : nullptr;
            close(mfd);
            if (h) { LOGI("loader loaded from memfd"); return; }
            LOGE("memfd: %s", ok ? dlerror() : "write failed");
        }

        if (!dataDir_[0]) return;
        char path[600];
        snprintf(path, sizeof path, "%s/code_cache", dataDir_);
        mkdir(path, 0771);
        snprintf(path, sizeof path, "%s/code_cache/%s", dataDir_, kLoaderName);
        if (!WriteAll(path, loader_.data, loader_.size)) { LOGE("cannot write %s", path); return; }
        chmod(path, 0500);
        if (dlopen(path, RTLD_NOW)) LOGI("loader loaded from %s", path);
        else LOGE("%s: %s", path, dlerror());
    }

    zygisk::Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool target_ = false;
    int loaderFd_ = -1;
    char dataDir_[400] = {};
    Blob loader_, version_;
    Blob bundle_[kMaxBundle];
    int bundleCount_ = 0;
};

}  // namespace

REGISTER_ZYGISK_MODULE(ArkModule)
