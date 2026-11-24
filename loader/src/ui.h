// In-game UI owned by the loader: the MODS button + panel made of UMG widgets (mods list with ON/OFF
// and each mod's toggles; in the main menu a MODS entry copied from EXIT), plus draw/touch services
// for mods (HUD canvas after AShooterHUD::DrawHUD; touches read at the Slate level).
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

// Mod windows (any thread; widgets are built on the game thread when first shown).
// Ids are > 0; 0 means failure. A window is listed under its mod in the MODS panel.
int NewWindow(const char* title, const void* owner);
int AddLabel(int window, const char* text);
int AddButton(int window, const char* text, ArkUiFn fn, void* user);
int AddCheck(int window, const char* label, int* value, ArkUiChangedFn fn, void* user);
int AddNumber(int window, const char* label, float* value, float min, float max, float step,
              ArkUiChangedFn fn, void* user);
void SetText(int id, const char* text);  // a control's text or a window's title
void ShowWindow(int window, int mode);   // 1 show, 0 hide, -1 toggle
bool WindowVisible(int window);

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
    bool hasSettings;      // its <mod>.ini has settings (other than remembered toggles)
};

// One "key = value" line of a mod's ini, for the SETTINGS window.
struct Setting {
    std::string key, value;
    bool ranged;           // the mod declared min/max/step for it
    float min, max, step;
};

}  // namespace ui
