// Шаблон C++-мода для ArkModLoader (API v3). Сборка — см. mods/QUICKSTART.md.
// То же, что my_mod.lua: переключатель, настройка, окно и восстановление при выгрузке.
#include <arkmod.h>

namespace {

const ArkModAPI* api;
int gEnabled = 0;           // переключатель в панели MODS
float gSpeed = 1200;        // настройка walk_speed (окно SETTINGS)
int gTaps = 0;
int gLabel = 0;             // id надписи в окне

UObject* gPCClass;
int oPlayer = -1, oPawn = -1, oMove = -1, oMaxWalk = -1;
float gOriginal = -1;       // что было в игре до нас
UObject* gChanged = nullptr;  // компонент, который мы поменяли

UObject* LocalPawn() {
    struct Ctx { UObject* pawn; } c{nullptr};
    api->for_each_object(gPCClass, [](UObject* pc, void* u) {
        auto* c = static_cast<Ctx*>(u);
        auto* base = reinterpret_cast<char*>(pc);
        if (!*reinterpret_cast<void**>(base + oPlayer)) return 1;  // не локальный игрок
        c->pawn = *reinterpret_cast<UObject**>(base + oPawn);
        return 0;
    }, &c);
    return c.pawn;
}

float& MaxWalk(UObject* move) { return *reinterpret_cast<float*>(reinterpret_cast<char*>(move) + oMaxWalk); }

void Tick(void*) {
    UObject* pawn = LocalPawn();
    UObject* move = pawn ? *reinterpret_cast<UObject**>(reinterpret_cast<char*>(pawn) + oMove) : nullptr;
    if (!move) return;
    if (gEnabled) {
        if (gChanged != move) { gChanged = move; gOriginal = MaxWalk(move); }
        MaxWalk(move) = gSpeed;
    } else if (gChanged) {
        MaxWalk(gChanged) = gOriginal;
        gChanged = nullptr;
    }
}

void OnButton(void*) {
    ++gTaps;
    api->log("my_mod: button tapped %d time(s)", gTaps);
    api->ui_set_text(gLabel, "Logged to loader.log");
}

}  // namespace

extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "My C++ Mod"; }

extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) {
    api = a;
    if (a->version < 3) return 1;  // нужен загрузчик 0.9+

    gPCClass = a->find_object("/Script/Engine.PlayerController");
    UObject* controller = a->find_object("/Script/Engine.Controller");
    UObject* character = a->find_object("/Script/Engine.Character");
    UObject* movement = a->find_object("/Script/Engine.CharacterMovementComponent");
    if (!gPCClass || !controller || !character || !movement) return 2;
    oPlayer = a->property_offset(gPCClass, "Player");
    oPawn = a->property_offset(controller, "Pawn");
    oMove = a->property_offset(character, "CharacterMovement");
    oMaxWalk = a->property_offset(movement, "MaxWalkSpeed");
    if (oPlayer < 0 || oPawn < 0 || oMove < 0 || oMaxWalk < 0) return 3;

    a->config_range("walk_speed", 100, 5000, 100);
    gSpeed = a->config_float("walk_speed", 1200);
    a->ui_toggle("MY FEATURE", &gEnabled);

    int win = a->ui_window("My C++ Mod");
    gLabel = a->ui_label(win, "Hello!");
    a->ui_button(win, "SAY HELLO", OnButton, nullptr);
    a->ui_number(win, "WALK SPEED", &gSpeed, 100, 5000, 100,
                 [](void*, float v) {
                     char buf[32];
                     __builtin_snprintf(buf, sizeof buf, "%g", v);
                     api->config_set("walk_speed", buf);
                 }, nullptr);

    a->on_tick(Tick, nullptr);
    return 0;
}

extern "C" __attribute__((visibility("default"))) void ArkMod_Shutdown() {
    if (gChanged) MaxWalk(gChanged) = gOriginal;  // вернуть как было
}
