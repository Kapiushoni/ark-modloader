// Cheat Menu (single-player), API v2: toggles live in the loader's MODS panel
// (Fly, God mode, Infinite stats, Speed). While flying, big UP / DOWN buttons appear:
//   short tap  -> keep rising / descending until tapped again (or the other button)
//   long press -> move only while held
// Settings (ArkMods/cheat_menu.ini): fly_speed, vertical_speed, speed_multiplier.
// No inline hooks, so the mod can be switched off / hot-reloaded at any time.
#include <arkmod.h>

#include <time.h>

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
UObject* gSetMoveMode;
int oMoveMode = -1, oMoveCustom = -1;
int oVelocity = -1, oMaxFly = -1, oMaxWalk = -1, oCurStats = -1, oMaxStats = -1, oMovementMode = -1;
int gFrame = 0;

// ------------------------------------------------------------------ state
int gFly, gGod, gStats, gSpeed;          // toggles (driven by the MODS panel)
int gFlyApplied, gSpeedApplied;          // what is currently applied to the pawn
float gOrigWalk = 0;
int gVertical = 0;                       // +1 rising, -1 descending, 0 hover
float gFlySpeed = 1200, gVerticalSpeed = 900, gSpeedMul = 3;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Hit(float px, float py) const { return w > 0 && px >= x && px <= x + w && py >= y && py <= y + h; }
};
Rect gUpBtn, gDownBtn;

// ------------------------------------------------------------------ helpers
void SetMoveMode(uint8_t mode) {
    if (!gMove || !gSetMoveMode) return;
    char p[16] = {};
    p[oMoveMode] = char(mode);
    p[oMoveCustom] = 0;
    api->process_event(gMove, gSetMoveMode, p);
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
    gFlyApplied = gSpeedApplied = 0;  // new pawn: re-apply
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
}

void ReadConfig() {
    gFlySpeed = api->config_float("fly_speed", 1200.f);
    gVerticalSpeed = api->config_float("vertical_speed", 900.f);
    gSpeedMul = api->config_float("speed_multiplier", 3.f);
}

// Bring the pawn in line with the toggles (edges only, plus per-frame upkeep).
void Apply() {
    if (gFly != gFlyApplied && gMove) {
        gFlyApplied = gFly;
        gVertical = 0;
        SetMoveMode(gFly ? 5 /*MOVE_Flying*/ : 3 /*MOVE_Falling*/);
    }
    if (gSpeed != gSpeedApplied && gMove && oMaxWalk >= 0) {
        gSpeedApplied = gSpeed;
        if (gSpeed) gOrigWalk = F<float>(gMove, oMaxWalk);
        else if (gOrigWalk > 0) F<float>(gMove, oMaxWalk) = gOrigWalk;
    }

    if (gFly && gMove) {
        if (oMovementMode >= 0 && F<uint8_t>(gMove, oMovementMode) != 5) SetMoveMode(5);
        if (oMaxFly >= 0) F<float>(gMove, oMaxFly) = gFlySpeed;
        if (oVelocity >= 0) {
            float* v = reinterpret_cast<float*>(reinterpret_cast<char*>(gMove) + oVelocity);
            if (gVertical) v[2] = gVerticalSpeed * gVertical;
            else if (v[2] > -50.f && v[2] < 50.f) v[2] = 0.f;  // hover: kill leftover drift
        }
    } else {
        gVertical = 0;
    }
    if (gStatus && oCurStats >= 0 && oMaxStats >= 0) {
        float* cur = reinterpret_cast<float*>(reinterpret_cast<char*>(gStatus) + oCurStats);
        float* max = reinterpret_cast<float*>(reinterpret_cast<char*>(gStatus) + oMaxStats);
        if (gGod) { cur[0] = max[0]; cur[2] = 0; }                // health, torpidity
        if (gStats) for (int i : {1, 3, 4, 5}) cur[i] = max[i];  // stamina, oxygen, food, water
    }
    if (gSpeed && gMove && oMaxWalk >= 0 && gOrigWalk > 0) F<float>(gMove, oMaxWalk) = gOrigWalk * gSpeedMul;
}

void Tick(void*) {
    ++gFrame;
    char world[64];
    api->object_name(api->get_world(), world, sizeof world);
    if (!strcmp(world, "MainMenu")) { gPawn = nullptr; return; }
    if (gFrame % 30 == 0 || !gPC) { Refresh(); ReadConfig(); }
    Apply();
}

// ------------------------------------------------------------------ UP / DOWN buttons
void Draw(void*, float w, float h) {
    if (!gFly || !gPawn) { gUpBtn = gDownBtn = Rect{}; return; }
    float s = h / 900.f;
    gUpBtn = {w - 260 * s, 290 * s, 220 * s, 140 * s};
    gDownBtn = {w - 260 * s, 450 * s, 220 * s, 140 * s};
    for (int i = 0; i < 2; ++i) {
        const Rect& r = i ? gDownBtn : gUpBtn;
        bool active = gVertical == (i ? -1 : 1);
        api->draw_rect(r.x, r.y, r.w, r.h, active ? 0.1f : 0.05f, active ? 0.55f : 0.25f, active ? 0.25f : 0.45f,
                       active ? 0.95f : 0.75f);
        api->draw_text(i ? "DOWN" : "UP", r.x + 28 * s, r.y + r.h / 2 - 22 * s, 2.4f * s, 1, 1, 1);
    }
}

constexpr double kHoldThreshold = 0.35;  // longer press = "move while held"
struct Press { int dir = 0; double start = 0; bool stopped = false; };
Press gPress[16];

int Touch(void*, int phase, int finger, float x, float y) {
    if (finger < 0 || finger >= 16) return 0;
    Press& p = gPress[finger];
    if (phase == 0) {
        int dir = gUpBtn.Hit(x, y) ? 1 : gDownBtn.Hit(x, y) ? -1 : 0;
        if (!dir) return 0;
        p = {dir, Now(), gVertical == dir};  // tapping the active direction stops
        gVertical = p.stopped ? 0 : dir;
        return 1;
    }
    if (phase == 2) {
        if (p.dir && !p.stopped && Now() - p.start > kHoldThreshold && gVertical == p.dir) gVertical = 0;
        p = Press{};
    }
    return 1;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "Cheat Menu"; }

extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) {
    api = a;
    if (a->version < 2) return 1;
    gPCClass = a->find_object("/Script/Engine.PlayerController");
    UObject* ctrl = a->find_object("/Script/Engine.Controller");
    UObject* cmc = a->find_object("/Script/Engine.CharacterMovementComponent");
    if (!gPCClass || !ctrl || !cmc) return 2;
    gPlayerOff = a->property_offset(gPCClass, "Player");
    oPawnRef = a->property_offset(ctrl, "Pawn");
    gSetMoveMode = a->find_function(cmc, "SetMovementMode");
    if (!gSetMoveMode || a->params_size(gSetMoveMode) > 16) return 3;
    oMoveMode = a->property_offset(gSetMoveMode, "NewMovementMode");
    oMoveCustom = a->property_offset(gSetMoveMode, "NewCustomMode");
    if (gPlayerOff < 0 || oPawnRef < 0 || oMoveMode < 0 || oMoveCustom < 0) return 4;

    if (a->version >= 3) {  // what the SETTINGS window offers for each value
        a->config_range("fly_speed", 100, 5000, 100);
        a->config_range("vertical_speed", 100, 3000, 100);
        a->config_range("speed_multiplier", 1, 10, 0.5f);
    }
    ReadConfig();
    a->ui_toggle("FLY", &gFly);
    a->ui_toggle("GOD MODE", &gGod);
    a->ui_toggle("INFINITE STATS", &gStats);
    a->ui_toggle("SPEED BOOST", &gSpeed);
    a->on_draw(Draw, nullptr);
    a->on_touch(Touch, nullptr);
    a->on_tick(Tick, nullptr);
    return 0;
}

// Leave the player as we found them (called before unload / hot-reload).
extern "C" __attribute__((visibility("default"))) void ArkMod_Shutdown() {
    gFly = gSpeed = gGod = gStats = 0;
    if (gPawn) Apply();
}
