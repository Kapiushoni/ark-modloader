#pragma once
#include <string>

namespace mods {
void SetModsDir(const std::string& dir);
int LoadNew(const std::string& cacheDir);  // loads mods not seen before; returns count initialised
}  // namespace mods
