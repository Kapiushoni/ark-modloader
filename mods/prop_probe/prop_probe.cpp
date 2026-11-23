// Research: layout of UProperty subclasses (expected extra data at +0x78).
#include <arkmod.h>
#include <cstdint>
#include <cstring>
namespace {
const ArkModAPI* api;
template <typename T> T F(void* o, int off) { return *reinterpret_cast<T*>(reinterpret_cast<char*>(o) + off); }
struct Find { UObject* want; bool found; };
int Visit(UObject* o, void* u) { auto* f = static_cast<Find*>(u); if (o == f->want) { f->found = true; return 0; } return 1; }
bool IsObject(UObject* p) {
    if (!p || (reinterpret_cast<uintptr_t>(p) & 7)) return false;
    Find f{p, false};
    api->for_each_object(nullptr, Visit, &f);
    return f.found;
}
void Name(UObject* o, char* b) { if (o) api->object_name(o, b, 128); else strcpy(b, "null"); }
void Dump(const char* cls, const char* want) {
    UObject* c = api->find_object(cls);
    for (UObject* s = c; s; s = F<UObject*>(s, 0x30))
        for (UObject* f = F<UObject*>(s, 0x38); f; f = F<UObject*>(f, 0x28)) {
            char n[128], t[128], x[128];
            Name(f, n);
            if (!strstr(want, n)) continue;
            Name(api->object_class(f), t);
            UObject* p78 = F<UObject*>(f, 0x78);
            UObject* p80 = F<UObject*>(f, 0x80);
            uint8_t b[4]; memcpy(b, reinterpret_cast<char*>(f) + 0x78, 4);
            char y[128]; Name(IsObject(p78) ? p78 : nullptr, x);
            Name(IsObject(p80) ? p80 : nullptr, y);
            api->log("probe: %s.%s (%s) size=%d off=%d +78=%s +80=%s bytes78=%02x %02x %02x %02x",
                     cls, n, t, F<int32_t>(f, 0x34), F<int32_t>(f, 0x50), x, y, b[0], b[1], b[2], b[3]);
        }
}
int gDone = 0;
void Tick(void*) {
    if (gDone++) return;
    Dump("/Script/UMG.TextBlock", "|ColorAndOpacity|Font|bWrapWithInvalidationPanel|DontApplyDisabledEffect|MinDesiredWidth|");
    Dump("/Script/Engine.Actor", "|RootComponent|Tags|bHidden|bCanBeDamaged|Role|CustomTimeDilation|");
    Dump("/Script/Engine.CharacterMovementComponent", "|MovementMode|JumpZVelocity|Velocity|");
}
}
extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) { api = a; a->on_tick(Tick, nullptr); return 0; }
