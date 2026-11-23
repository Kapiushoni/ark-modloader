// Test mod: in a game map, finds the local player controller + pawn via reflection,
// logs the pawn's position every few seconds and tries a console command once.
#include <arkmod.h>

#include <cstring>

namespace {

const ArkModAPI* api;
UObject* gPCClass;
int gPlayerOff = -1, gPawnOff = -1, gRootOff = -1, gLocOff = -1;
int gFrame = 0;
bool gCmdDone = false;

struct Found { UObject* pc; };

int FindLocalPC(UObject* o, void* user) {
    if (*reinterpret_cast<void**>(reinterpret_cast<char*>(o) + gPlayerOff)) {
        static_cast<Found*>(user)->pc = o;
        return 0;
    }
    return 1;
}

template <typename T>
T Field(UObject* o, int off) { return *reinterpret_cast<T*>(reinterpret_cast<char*>(o) + off); }

void Tick(void*) {
    if (++gFrame % 150) return;  // ~every 5 s at 30 fps
    char world[128];
    api->object_name(api->get_world(), world, sizeof world);
    if (strcmp(world, "MainMenu") == 0) return;

    Found f{nullptr};
    api->for_each_object(gPCClass, FindLocalPC, &f);
    if (!f.pc) { api->log("probe: [%s] no local PlayerController yet", world); return; }

    UObject* pawn = Field<UObject*>(f.pc, gPawnOff);
    char pcCls[128] = "?", pawnCls[128] = "None";
    api->object_name(api->object_class(f.pc), pcCls, sizeof pcCls);
    if (pawn) api->object_name(api->object_class(pawn), pawnCls, sizeof pawnCls);

    float loc[3] = {0, 0, 0};
    if (pawn) {
        if (gRootOff < 0) gRootOff = api->property_offset(pawn, "RootComponent");
        UObject* root = gRootOff >= 0 ? Field<UObject*>(pawn, gRootOff) : nullptr;
        if (root && gLocOff < 0) gLocOff = api->property_offset(root, "RelativeLocation");
        if (root && gLocOff >= 0) std::memcpy(loc, reinterpret_cast<char*>(root) + gLocOff, sizeof loc);
    }
    api->log("probe: [%s] PC=%s pawn=%s pos=(%.0f, %.0f, %.0f)", world, pcCls, pawnCls, loc[0], loc[1], loc[2]);

    if (!gCmdDone && pawn) {
        gCmdDone = true;
        int ok = api->exec_command("stat fps");
        api->log("probe: exec_command(\"stat fps\") -> %d", ok);
    }
}

}  // namespace

extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "In-game Probe"; }

extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) {
    api = a;
    gPCClass = a->find_object("/Script/Engine.PlayerController");
    UObject* ctrl = a->find_object("/Script/Engine.Controller");
    if (!gPCClass || !ctrl) return 1;
    gPlayerOff = a->property_offset(gPCClass, "Player");
    gPawnOff = a->property_offset(ctrl, "Pawn");
    a->log("probe: PlayerController.Player@%d Controller.Pawn@%d", gPlayerOff, gPawnOff);
    if (gPlayerOff < 0 || gPawnOff < 0) return 2;
    a->on_tick(Tick, nullptr);
    return 0;
}
