// Research mod 3: cheat permission flags + direct SetMovementMode.
#include <arkmod.h>
#include <cstring>

namespace {
const ArkModAPI* api;
int gFrame = 0, gStep = 0;
UObject *gPC, *gCM, *gMove;
int gModeOff = -1;
template <typename T> T& F(void* o, int off) { return *reinterpret_cast<T*>(reinterpret_cast<char*>(o) + off); }

int FindPC(UObject* o, void* out) {
    static int playerOff = api->property_offset(api->find_object("/Script/Engine.PlayerController"), "Player");
    if (F<void*>(o, playerOff)) { *static_cast<UObject**>(out) = o; return 0; }
    return 1;
}
int Mode() { return F<uint8_t>(gMove, gModeOff); }

// Calls obj.fn with up to two named params (byte-sized), returns value of "ReturnValue" byte or -1.
int Call(UObject* obj, const char* fn, const char* p1 = nullptr, int v1 = 0, const char* p2 = nullptr, int v2 = 0) {
    UObject* f = api->find_function(obj, fn);
    if (!f) { api->log("probe3: %s not found", fn); return -2; }
    char params[128] = {};
    if (p1) { int o = api->property_offset(f, p1); if (o >= 0) params[o] = char(v1); else api->log("probe3: no param %s", p1); }
    if (p2) { int o = api->property_offset(f, p2); if (o >= 0) params[o] = char(v2); }
    api->process_event(obj, f, params);
    int ro = api->property_offset(f, "ReturnValue");
    int rv = ro >= 0 ? params[ro] : -1;
    api->log("probe3: %s -> ret=%d, MovementMode=%d", fn, rv, Mode());
    return rv;
}
void Tick(void*) {
    if (++gFrame % 60) return;
    char world[64]; api->object_name(api->get_world(), world, sizeof world);
    if (!strcmp(world, "MainMenu")) return;
    if (gStep == 0) {
        gPC = nullptr;
        api->for_each_object(api->find_object("/Script/Engine.PlayerController"), FindPC, &gPC);
        if (!gPC) return;
        UObject* pawn = F<UObject*>(gPC, api->property_offset(gPC, "Pawn"));
        gCM = F<UObject*>(gPC, api->property_offset(gPC, "CheatManager"));
        if (!pawn || !gCM) return;
        gMove = F<UObject*>(pawn, api->property_offset(pawn, "CharacterMovement"));
        gModeOff = api->property_offset(gMove, "MovementMode");
    }
    switch (gStep++) {
    case 0: Call(gPC, "IsAllowedToCheat"); break;
    case 1: Call(gPC, "SetCheatPlayer", "bSwitch", 1); Call(gPC, "IsAllowedToCheat"); break;
    case 2: Call(gCM, "Fly"); break;
    case 4: api->log("probe3: 2s after Fly MovementMode=%d", Mode());
            if (Mode() != 5) Call(gMove, "SetMovementMode", "NewMovementMode", 5, "NewCustomMode", 0);
            break;
    case 6: api->log("probe3: 2s after SetMovementMode MovementMode=%d", Mode()); break;
    case 9: Call(gMove, "SetMovementMode", "NewMovementMode", 1, "NewCustomMode", 0);
            Call(gPC, "SetCheatPlayer", "bSwitch", 0); break;
    case 10: api->log("probe3: done MovementMode=%d", Mode()); break;
    }
}
}  // namespace
extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "Cheat Probe 3"; }
extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) { api = a; a->on_tick(Tick, nullptr); return 0; }
