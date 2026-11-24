// Mod registry: discovery, load / unload / hot-reload, enable state (ArkMods/mods.ini),
// per-mod settings (ArkMods/<mod>.ini) and the ArkModAPI table.
#pragma once
#include <string>
#include <vector>

#include "ui.h"

namespace mods {

void Setup(const std::string& modsDir, const std::string& cacheDir);

// Loader thread: compare the folder with the registry and queue loads/reloads/unloads
// (they run on the game thread).
void Scan();

// Game thread.
void SetEnabled(const std::string& file, bool enabled);
std::vector<ui::ModRow> PanelRows();

// Panel toggles are remembered per mod as "toggle.<label> = 0/1" in ArkMods/<mod>.ini.
void RestoreToggle(const void* owner, const std::string& label, int* value);
void SaveToggle(const void* owner, const std::string& label, int value);

}  // namespace mods
