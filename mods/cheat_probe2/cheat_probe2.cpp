// Research mod 2: call ShooterCheatManager UFunctions directly via ProcessEvent.
#include <arkmod.h>
#include <cstring>

namespace {
const ArkModAPI* api;
int gFrame = 0, gStep = 0;
UObject *gCM, *gMove;
int gModeOff = -1;
template <typename T> T& F(void* o, int off) { return *reinterpret_cast<T*>(reinterpret_cast<char*>(o) + off); }

int FindPC(UObject* o, void* out) {
    static int playerOff = api->property_offset(api->find_object("/Script/Engine.PlayerController"), "Player");
    if (F<void*>(o, playerOff)) { *static_cast<UObject**>(out) = o; return 0; }
    return 1;
}
void Call(const char* fn) {
    UObject* f = api->find_function(gCM, fn);
    char params[64] = {};
    if (!f) { api->log("probe2: %s not found", fn); return; }
    api->process_event(gCM, f, params);
    api->log("probe2: called %s (params=%d) -> MovementMode=%d", fn, api->params_size(f), F<uint8_t>(gMove, gModeOff));
}
void Tick(void*) {
    if (++gFrame % 60) return;
    char world[64]; api->object_name(api->get_world(), world, sizeof world);
    if (!strcmp(world, "MainMenu")) return;
    if (gStep == 0) {
        UObject* pc = nullptr;
        api->for_each_object(api->find_object("/Script/Engine.PlayerController"), FindPC, &pc);
        if (!pc) return;
        UObject* pawn = F<UObject*>(pc, api->property_offset(pc, "Pawn"));
        gCM = F<UObject*>(pc, api->property_offset(pc, "CheatManager"));
        if (!pawn || !gCM) return;
        gMove = F<UObject*>(pawn, api->property_offset(pawn, "CharacterMovement"));
        gModeOff = api->property_offset(gMove, "MovementMode");
        api->log("probe2: start MovementMode=%d", F<uint8_t>(gMove, gModeOff));
    }
    switch (gStep++) {
    case 1: Call("Fly"); break;
    case 3: Call("Walk"); break;
    case 4: api->log("probe2: done, MovementMode=%d", F<uint8_t>(gMove, gModeOff)); break;
    }
}
}  // namespace
extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "Cheat Probe 2"; }
extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) { api = a; a->on_tick(Tick, nullptr); return 0; }
