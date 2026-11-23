// Cheat Menu (single-player): a "MODS" button on the HUD opens a panel with toggles
// (Fly, God mode, Infinite stats, Speed x3). While flying, big UP / DOWN buttons appear:
//   short tap  -> keep rising / descending until tapped again (or the other button is tapped)
//   long press -> move only while held
#include <arkmod.h>

#include <time.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {

const ArkModAPI* api;

template <typename T>
T& F(void* o, int off) { return *reinterpret_cast<T*>(reinterpret_cast<char*>(o) + off); }

double Now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ------------------------------------------------------------------ engine handles
UObject* gPCClass;
int gPlayerOff = -1, oPawnRef = -1;
UObject *gPC, *gPawn, *gMove, *gStatus;
int gFrame = 0;

struct Fn {
    UObject* f = nullptr;
    int size = 0;
};
Fn gDrawRect, gDrawText, gSetMoveMode;
int oRectColor, oRectX, oRectY, oRectW, oRectH;
int oTxt, oTxtColor, oTxtX, oTxtY, oTxtFont, oTxtScale, oTxtScalePos;
int oMoveMode, oMoveCustom;
int oVelocity = -1, oMaxFly = -1, oMaxWalk = -1, oCurStats = -1, oMaxStats = -1, oMovementMode = -1;

Fn Lookup(const char* cls, const char* fn) {
    Fn r;
    UObject* c = api->find_object(cls);
    r.f = c ? api->find_function(c, fn) : nullptr;
    r.size = r.f ? api->params_size(r.f) : 0;
    if (!r.f) api->log("cheat_menu: missing %s.%s", cls, fn);
    return r;
}
int Off(Fn& fn, const char* p) {
    int o = fn.f ? api->property_offset(fn.f, p) : -1;
    if (o < 0) api->log("cheat_menu: missing param %s", p);
    return o;
}

// ------------------------------------------------------------------ state
enum Toggle { kFly, kGod, kStats, kSpeed, kCount };
const char* kLabels[kCount] = {"FLY", "GOD MODE", "INFINITE STATS", "SPEED x3"};
bool gOn[kCount] = {};
bool gMenuOpen = false;
float gOrigWalk = 0;

int gVertical = 0;  // flight: +1 rising, -1 descending, 0 hover
constexpr float kFlySpeed = 1200.f, kVerticalSpeed = 900.f;
constexpr double kHoldThreshold = 0.35;  // longer press = "move while held"

struct Rect {
    float x, y, w, h;
    bool Hit(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};
Rect gModsBtn, gRows[kCount], gUpBtn, gDownBtn;
float gScreenW = 0, gScreenH = 0;
bool gWorldIsMenu = true;

// ------------------------------------------------------------------ helpers
void DrawRect(UObject* hud, const Rect& r, float cr, float cg, float cb, float ca) {
    char p[64] = {};
    float* c = reinterpret_cast<float*>(p + oRectColor);
    c[0] = cr; c[1] = cg; c[2] = cb; c[3] = ca;
    F<float>(p, oRectX) = r.x; F<float>(p, oRectY) = r.y; F<float>(p, oRectW) = r.w; F<float>(p, oRectH) = r.h;
    api->process_event(hud, gDrawRect.f, p);
}

void DrawText(UObject* hud, const char* s, float x, float y, float scale, float cr = 1, float cg = 1, float cb = 1) {
    wchar_t w[64];
    int n = 0;
    for (; s[n] && n < 63; ++n) w[n] = wchar_t(static_cast<unsigned char>(s[n]));
    w[n] = 0;
    char p[96] = {};
    struct { wchar_t* data; int32_t num, max; } str{w, n + 1, n + 1};
    std::memcpy(p + oTxt, &str, sizeof str);
    float* c = reinterpret_cast<float*>(p + oTxtColor);
    c[0] = cr; c[1] = cg; c[2] = cb; c[3] = 1;
    F<float>(p, oTxtX) = x; F<float>(p, oTxtY) = y;
    F<void*>(p, oTxtFont) = nullptr;  // engine medium font
    F<float>(p, oTxtScale) = scale;
    F<bool>(p, oTxtScalePos) = false;
    api->process_event(hud, gDrawText.f, p);
}

void SetMoveMode(uint8_t mode) {
    if (!gMove) return;
    char p[16] = {};
    p[oMoveMode] = char(mode);
    p[oMoveCustom] = 0;
    api->process_event(gMove, gSetMoveMode.f, p);
}

int FindPC(UObject* o, void* out) {
    if (F<void*>(o, gPlayerOff)) { *static_cast<UObject**>(out) = o; return 0; }
    return 1;
}

void Refresh() {  // (re)resolve player objects; pawn changes on respawn / mounting
    gPC = nullptr;
    api->for_each_object(gPCClass, FindPC, &gPC);
    UObject* pawn = gPC && oPawnRef >= 0 ? F<UObject*>(gPC, oPawnRef) : nullptr;
    if (pawn == gPawn) return;
    gPawn = pawn;
    gMove = gStatus = nullptr;
    if (!gPawn) return;
    int mo = api->property_offset(gPawn, "CharacterMovement");
    gMove = mo >= 0 ? F<UObject*>(gPawn, mo) : nullptr;
    if (gMove && oVelocity < 0) {
        oVelocity = api->property_offset(gMove, "Velocity");
        oMaxFly = api->property_offset(gMove, "MaxFlySpeed");
        oMaxWalk = api->property_offset(gMove, "MaxWalkSpeed");
        oMovementMode = api->property_offset(gMove, "MovementMode");
    }
    if (UObject* f = api->find_function(gPawn, "GetCharacterStatusComponent")) {
        char p[16] = {};
        api->process_event(gPawn, f, p);
        gStatus = F<UObject*>(p, api->property_offset(f, "ReturnValue"));
    }
    if (gStatus && oCurStats < 0) {
        oCurStats = api->property_offset(gStatus, "CurrentStatusValues");
        oMaxStats = api->property_offset(gStatus, "MaxStatusValues");
    }
    char cls[128] = "?";
    api->object_name(api->object_class(gPawn), cls, sizeof cls);
    api->log("cheat_menu: pawn=%s move=%p status=%p", cls, gMove, gStatus);
}

void SetToggle(int i, bool on) {
    gOn[i] = on;
    api->log("cheat_menu: %s %s", kLabels[i], on ? "ON" : "OFF");
    if (i == kFly) {
        gVertical = 0;
        SetMoveMode(on ? 5 /*MOVE_Flying*/ : 3 /*MOVE_Falling*/);
    }
    if (i == kSpeed && gMove && oMaxWalk >= 0) {
        if (on) gOrigWalk = F<float>(gMove, oMaxWalk);
        else if (gOrigWalk > 0) F<float>(gMove, oMaxWalk) = gOrigWalk;
    }
}

// ------------------------------------------------------------------ per-frame
void ApplyCheats() {
    if (gOn[kFly] && gMove) {
        if (oMovementMode >= 0 && F<uint8_t>(gMove, oMovementMode) != 5) SetMoveMode(5);
        if (oMaxFly >= 0) F<float>(gMove, oMaxFly) = kFlySpeed;
        if (oVelocity >= 0) {
            float* v = reinterpret_cast<float*>(reinterpret_cast<char*>(gMove) + oVelocity);
            if (gVertical) v[2] = kVerticalSpeed * gVertical;
            else if (v[2] > -50.f && v[2] < 50.f) v[2] = 0.f;  // hover: kill leftover drift
        }
    }
    if (gStatus && oCurStats >= 0 && oMaxStats >= 0) {
        float* cur = reinterpret_cast<float*>(reinterpret_cast<char*>(gStatus) + oCurStats);
        float* max = reinterpret_cast<float*>(reinterpret_cast<char*>(gStatus) + oMaxStats);
        if (gOn[kGod]) { cur[0] = max[0]; cur[2] = 0; }                // health, torpidity
        if (gOn[kStats]) for (int i : {1, 3, 4, 5}) cur[i] = max[i];  // stamina, oxygen, food, water
    }
    if (gOn[kSpeed] && gMove && oMaxWalk >= 0 && gOrigWalk > 0) F<float>(gMove, oMaxWalk) = gOrigWalk * 3.f;
}

void Tick(void*) {
    ++gFrame;
    char world[64];
    api->object_name(api->get_world(), world, sizeof world);
    gWorldIsMenu = !strcmp(world, "MainMenu");
    if (gWorldIsMenu) { gPawn = nullptr; return; }
    if (gFrame % 30 == 0 || !gPC) Refresh();
    ApplyCheats();
}

// ------------------------------------------------------------------ touch input
// ARK's mobile UI swallows touches before PlayerInput, so we intercept them at the Slate
// level (FSlateApplication::OnTouch*). Touches that start on our UI are consumed.
struct Vec2 { float x, y; };
constexpr int kMaxTouch = 16;
struct TouchInfo {
    bool ours = false;
    int dir = 0;              // +1 UP button, -1 DOWN button, 0 other
    double start = 0;
    bool stoppedOnPress = false;  // the press turned movement off -> release does nothing
};
TouchInfo gTouch[kMaxTouch];

bool (*gOrigTouchStarted)(void*, const void*, const Vec2&, int, int);
bool (*gOrigTouchMoved)(void*, const Vec2&, int, int);
bool (*gOrigTouchEnded)(void*, const Vec2&, int, int);

bool OnOurUi(const Vec2& p, TouchInfo& t) {
    t = TouchInfo{};
    if (gScreenW == 0 || gWorldIsMenu) return false;
    if (gOn[kFly]) {
        int dir = gUpBtn.Hit(p.x, p.y) ? 1 : gDownBtn.Hit(p.x, p.y) ? -1 : 0;
        if (dir) {
            t.dir = dir;
            t.start = Now();
            t.stoppedOnPress = gVertical == dir;  // tapping the active direction stops
            gVertical = t.stoppedOnPress ? 0 : dir;
            return t.ours = true;
        }
    }
    if (gModsBtn.Hit(p.x, p.y)) { gMenuOpen = !gMenuOpen; return t.ours = true; }
    if (!gMenuOpen) return false;
    for (int i = 0; i < kCount; ++i)
        if (gRows[i].Hit(p.x, p.y)) { SetToggle(i, !gOn[i]); return t.ours = true; }
    return false;
}

bool HookedTouchStarted(void* app, const void* win, const Vec2& loc, int idx, int ctrl) {
    TouchInfo scratch;
    TouchInfo& t = idx >= 0 && idx < kMaxTouch ? gTouch[idx] : scratch;
    if (OnOurUi(loc, t)) return true;
    return gOrigTouchStarted(app, win, loc, idx, ctrl);
}
bool HookedTouchMoved(void* app, const Vec2& loc, int idx, int ctrl) {
    if (idx >= 0 && idx < kMaxTouch && gTouch[idx].ours) return true;
    return gOrigTouchMoved(app, loc, idx, ctrl);
}
bool HookedTouchEnded(void* app, const Vec2& loc, int idx, int ctrl) {
    if (idx < 0 || idx >= kMaxTouch || !gTouch[idx].ours) return gOrigTouchEnded(app, loc, idx, ctrl);
    TouchInfo& t = gTouch[idx];
    // Long press = "move while held": stop on release. Short tap keeps moving.
    if (t.dir && !t.stoppedOnPress && Now() - t.start > kHoldThreshold && gVertical == t.dir) gVertical = 0;
    t = TouchInfo{};
    return true;
}

// ------------------------------------------------------------------ drawing (after native AShooterHUD::DrawHUD)
// ARK's HUD draws natively and never fires HUD.ReceiveDrawHUD, so we inline-hook DrawHUD itself.
void (*gOrigDrawHUD)(UObject*) = nullptr;
int oCanvas = -1, oSizeX = -1, oSizeY = -1;
bool gLoggedHud = false;

void DrawFlightButton(UObject* hud, const Rect& r, const char* label, bool active, float s) {
    DrawRect(hud, r, active ? 0.1f : 0.05f, active ? 0.55f : 0.25f, active ? 0.25f : 0.45f, active ? 0.95f : 0.75f);
    DrawText(hud, label, r.x + 28 * s, r.y + r.h / 2 - 22 * s, 2.4f * s);
}

void DrawMenu(UObject* hud) {
    if (oCanvas < 0) oCanvas = api->property_offset(hud, "Canvas");
    UObject* canvas = oCanvas >= 0 ? F<UObject*>(hud, oCanvas) : nullptr;
    if (!canvas) return;
    if (oSizeX < 0) { oSizeX = api->property_offset(canvas, "SizeX"); oSizeY = api->property_offset(canvas, "SizeY"); }
    gScreenW = float(F<int32_t>(canvas, oSizeX));
    gScreenH = float(F<int32_t>(canvas, oSizeY));
    if (!gLoggedHud) { gLoggedHud = true; api->log("cheat_menu: drawing on %.0fx%.0f canvas", gScreenW, gScreenH); }
    float s = gScreenH / 900.f;  // layout designed for 1600x900

    // Flight buttons: right edge, below the MODS button, visible whenever flying.
    if (gOn[kFly]) {
        gUpBtn = {gScreenW - 260 * s, 290 * s, 220 * s, 140 * s};
        gDownBtn = {gScreenW - 260 * s, 450 * s, 220 * s, 140 * s};
        DrawFlightButton(hud, gUpBtn, "UP", gVertical > 0, s);
        DrawFlightButton(hud, gDownBtn, "DOWN", gVertical < 0, s);
    } else {
        gUpBtn = gDownBtn = {0, 0, 0, 0};
    }

    gModsBtn = {gScreenW - 400 * s, 140 * s, 220 * s, 90 * s};
    DrawRect(hud, gModsBtn, 0.02f, 0.25f, 0.35f, gMenuOpen ? 0.95f : 0.8f);
    DrawText(hud, gMenuOpen ? "MODS  X" : "MODS", gModsBtn.x + 30 * s, gModsBtn.y + 18 * s, 2.4f * s);
    if (!gMenuOpen) return;

    float rw = 560 * s, rh = 95 * s, gap = 12 * s;
    float px = gScreenW - 400 * s - rw - 20 * s, py = 140 * s;
    for (int i = 0; i < kCount; ++i) {
        gRows[i] = {px, py + i * (rh + gap), rw, rh};
        DrawRect(hud, gRows[i], gOn[i] ? 0.05f : 0.1f, gOn[i] ? 0.45f : 0.1f, gOn[i] ? 0.15f : 0.12f, 0.88f);
        DrawText(hud, kLabels[i], gRows[i].x + 24 * s, gRows[i].y + 22 * s, 2.1f * s);
        DrawText(hud, gOn[i] ? "ON" : "OFF", gRows[i].x + rw - 120 * s, gRows[i].y + 22 * s, 2.1f * s,
                 gOn[i] ? 0.4f : 1.f, gOn[i] ? 1.f : 0.4f, 0.4f);
    }
}

void HookedDrawHUD(UObject* hud) {
    gOrigDrawHUD(hud);
    DrawMenu(hud);
}

template <typename T>
bool Hook(T& orig, const char* sym, void* repl) {
    void* target = api->find_symbol(sym);
    orig = reinterpret_cast<T>(target ? api->inline_hook(target, repl) : nullptr);
    if (!orig) api->log("cheat_menu: failed to hook %s", sym);
    return orig != nullptr;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "Cheat Menu"; }

extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) {
    api = a;
    gPCClass = a->find_object("/Script/Engine.PlayerController");
    UObject* ctrl = a->find_object("/Script/Engine.Controller");
    if (!gPCClass || !ctrl) return 1;
    gPlayerOff = a->property_offset(gPCClass, "Player");
    oPawnRef = a->property_offset(ctrl, "Pawn");

    gDrawRect = Lookup("/Script/Engine.HUD", "DrawRect");
    gDrawText = Lookup("/Script/Engine.HUD", "DrawText");
    gSetMoveMode = Lookup("/Script/Engine.CharacterMovementComponent", "SetMovementMode");
    if (!gDrawRect.f || !gDrawText.f || !gSetMoveMode.f) return 2;

    oRectColor = Off(gDrawRect, "RectColor"); oRectX = Off(gDrawRect, "ScreenX"); oRectY = Off(gDrawRect, "ScreenY");
    oRectW = Off(gDrawRect, "ScreenW"); oRectH = Off(gDrawRect, "ScreenH");
    oTxt = Off(gDrawText, "Text"); oTxtColor = Off(gDrawText, "TextColor"); oTxtX = Off(gDrawText, "ScreenX");
    oTxtY = Off(gDrawText, "ScreenY"); oTxtFont = Off(gDrawText, "Font"); oTxtScale = Off(gDrawText, "Scale");
    oTxtScalePos = Off(gDrawText, "bScalePosition");
    oMoveMode = Off(gSetMoveMode, "NewMovementMode"); oMoveCustom = Off(gSetMoveMode, "NewCustomMode");
    if (gDrawRect.size > 64 || gDrawText.size > 96 || gSetMoveMode.size > 16) return 3;
    if (oRectColor < 0 || oTxt < 0 || oMoveMode < 0) return 4;

    // (All targets checked: >= 16 bytes long, no PC-relative instructions in the first 16.)
    bool ok = Hook(gOrigDrawHUD, "_ZN11AShooterHUD7DrawHUDEv", reinterpret_cast<void*>(&HookedDrawHUD));
    ok &= Hook(gOrigTouchStarted,
               "_ZN17FSlateApplication14OnTouchStartedERK10TSharedPtrI14FGenericWindowL7ESPMode0EERK9FVector2Dii",
               reinterpret_cast<void*>(&HookedTouchStarted));
    ok &= Hook(gOrigTouchMoved, "_ZN17FSlateApplication12OnTouchMovedERK9FVector2Dii",
               reinterpret_cast<void*>(&HookedTouchMoved));
    ok &= Hook(gOrigTouchEnded, "_ZN17FSlateApplication12OnTouchEndedERK9FVector2Dii",
               reinterpret_cast<void*>(&HookedTouchEnded));
    if (!ok) return 5;
    a->log("cheat_menu: hooks installed (HUD, touch)");
    a->on_tick(Tick, nullptr);
    return 0;
}
