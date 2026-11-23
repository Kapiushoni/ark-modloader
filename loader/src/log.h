// Logging to logcat (tag ArkModLoader) and to ArkMods/loader.log.
#pragma once
#include <cstdarg>

namespace logx {
void OpenFile(const char* path);
void Write(int prio, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void WriteV(int prio, const char* prefix, const char* fmt, va_list ap);
}  // namespace logx

#define LOG(...)  logx::Write(4 /*INFO*/, __VA_ARGS__)
#define LOGE(...) logx::Write(6 /*ERROR*/, __VA_ARGS__)
