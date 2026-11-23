#include "log.h"

#include <android/log.h>

#include <cstdio>
#include <ctime>
#include <mutex>

namespace logx {
namespace {
std::mutex gMu;
FILE* gFile = nullptr;
}

void OpenFile(const char* path) {
    std::lock_guard<std::mutex> l(gMu);
    if (gFile) fclose(gFile);
    gFile = fopen(path, "w");
}

void WriteV(int prio, const char* prefix, const char* fmt, va_list ap) {
    char msg[2048];
    int n = prefix ? snprintf(msg, sizeof msg, "[%s] ", prefix) : 0;
    vsnprintf(msg + n, sizeof msg - n, fmt, ap);
    __android_log_write(prio, "ArkModLoader", msg);
    std::lock_guard<std::mutex> l(gMu);
    if (gFile) {
        time_t t = time(nullptr);
        struct tm tm;
        localtime_r(&t, &tm);
        fprintf(gFile, "%02d:%02d:%02d %s%s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, prio >= 6 ? "ERROR " : "", msg);
        fflush(gFile);
    }
}

void Write(int prio, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    WriteV(prio, nullptr, fmt, ap);
    va_end(ap);
}

}  // namespace logx
