// In-game UI owned by the loader: the MODS button + panel (mods list with ON/OFF and each
// mod's toggles), plus draw/touch services for mods. Draws after AShooterHUD::DrawHUD and
// reads touches at the Slate level (ARK's mobile UI hides them from PlayerInput).
#pragma once
#include <string>
#include <vector>

#include "arkmod.h"

namespace ui {

bool Init();  // call once the engine is ready

void AddDraw(ArkDrawFn fn, void* user, const void* owner);
void AddTouch(ArkTouchFn fn, void* user, const void* owner);
void AddToggle(const char* label, int* value, const void* owner);
void RemoveOwner(const void* owner);

// Valid only inside draw callbacks (HUD canvas is set up there).
void DrawRect(float x, float y, float w, float h, float r, float g, float b, float a);
void DrawText(const char* ascii, float x, float y, float scale, float r, float g, float b);

// What the panel shows for one mod (provided by the mods module).
struct ModRow {
    std::string file;      // key for SetEnabled
    std::string name;
    bool enabled;          // user's choice (persisted)
    bool loaded;           // currently running
    std::string note;      // e.g. "restart the game to apply", "failed to load - see loader.log"
    const void* owner;     // module base while loaded (toggles are looked up by it)
};

}  // namespace ui
