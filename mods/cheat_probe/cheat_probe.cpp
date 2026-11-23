// Research mod: lists cheat-like functions and checks how fly/god work in single-player.
#include <arkmod.h>

#include <cstring>
#include <string>

namespace {

const ArkModAPI* api;
int gFrame = 0, gStep = 0;
UObject *gPC, *gPawn, *gMove;
int gModeOff = -1;

template <typename T>
T& F(void* o, int off) { return *reinterpret_cast<T*>(reinterpret_cast<char*>(o) + off); }

std::string Name(UObject* o) { char b[256]; api->object_name(o, b, sizeof b); return b; }

void ListFunctions(UObject* cls, const char* const* keys) {
    for (UObject* s = cls; s; s = F<UObject*>(s, 0x30)) {
        std::string line;
        for (UObject* f = F<UObject*>(s, 0x38); f; f = F<UObject*>(f, 0x28)) {
            if (Name(api->object_class(f)) != "Function") continue;
            std::string n = Name(f);
            for (const char* const* k = keys; *k; ++k)
                if (strcasestr(n.c_str(), *k)) { line += n + " "; break; }
        }
        if (!line.empty()) api->log("cheat_probe: %s: %s", Name(s).c_str(), line.c_str());
    }
}

int FindPC(UObject* o, void* out) {
    static int playerOff = api->property_offset(api->find_object("/Script/Engine.PlayerController"), "Player");
    if (F<void*>(o, playerOff)) { *static_cast<UObject**>(out) = o; return 0; }
    return 1;
}

void Tick(void*) {
    if (++gFrame % 60) return;
    char world[64];
    api->object_name(api->get_world(), world, sizeof world);
    if (!strcmp(world, "MainMenu")) return;

    switch (gStep) {
    case 0: {
        gPC = nullptr;
        api->for_each_object(api->find_object("/Script/Engine.PlayerController"), FindPC, &gPC);
        if (!gPC) return;
        gPawn = F<UObject*>(gPC, api->property_offset(gPC, "Pawn"));
        if (!gPawn) return;
        int cmOff = api->property_offset(gPC, "CheatManager");
        UObject* cm = cmOff >= 0 ? F<UObject*>(gPC, cmOff) : nullptr;
        api->log("cheat_probe: PC=%s CheatManager@%d=%s pawn=%s", Name(api->object_class(gPC)).c_str(), cmOff,
                 cm ? Name(api->object_class(cm)).c_str() : "null", Name(api->object_class(gPawn)).c_str());
        static const char* const keys[] = {"fly", "god", "walk", "ghost", "infinite", "cheat", "stat",
                                           "teleport", "time", "leveluP", "giveitem", nullptr};
        ListFunctions(api->object_class(gPC), keys);
        if (cm) ListFunctions(api->object_class(cm), keys);
        ListFunctions(api->object_class(gPawn), keys);

        int mOff = api->property_offset(gPawn, "CharacterMovement");
        gMove = mOff >= 0 ? F<UObject*>(gPawn, mOff) : nullptr;
        gModeOff = gMove ? api->property_offset(gMove, "MovementMode") : -1;
        api->log("cheat_probe: CharacterMovement=%s MovementMode@%d=%d", gMove ? Name(api->object_class(gMove)).c_str() : "null",
                 gModeOff, gModeOff >= 0 ? F<uint8_t>(gMove, gModeOff) : -1);
        gStep = 1;
        break;
    }
    case 1:
        api->log("cheat_probe: exec 'fly' -> %d", api->exec_command("fly"));
        gStep = 2;
        break;
    case 2:
        api->log("cheat_probe: after fly: MovementMode=%d", F<uint8_t>(gMove, gModeOff));
        api->log("cheat_probe: exec 'cheat fly' -> %d", api->exec_command("cheat fly"));
        gStep = 3;
        break;
    case 3:
        api->log("cheat_probe: after cheat fly: MovementMode=%d", F<uint8_t>(gMove, gModeOff));
        api->exec_command("walk");
        api->exec_command("cheat walk");
        gStep = 4;
        break;
    case 4:
        api->log("cheat_probe: after walk: MovementMode=%d — done", F<uint8_t>(gMove, gModeOff));
        gStep = 5;
        break;
    }
}

}  // namespace

extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "Cheat Probe"; }
extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) {
    api = a;
    a->on_tick(Tick, nullptr);
    return 0;
}
