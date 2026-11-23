// Example mod: renames the main-menu "PLAY ARK" button and counts actor BeginPlay calls.
#include <arkmod.h>

#include <cstring>

namespace {

const ArkModAPI* api;
UObject* gTextBlockClass;
UObject* gSetText;
int gTextOffset = -1, gInTextOffset = -1, gParamsSize = 0;
int gBeginPlays = 0;
UObject* gLastWorld = nullptr;
int gFrame = 0;

const char* kFrom = "PLAY ARK";
const char* kTo = "PLAY ARK (MODDED)";

int VisitTextBlock(UObject* tb, void*) {
    char text[256];
    api->text_to_utf8(reinterpret_cast<ArkText*>(reinterpret_cast<char*>(tb) + gTextOffset), text, sizeof text);
    if (strcmp(text, kFrom) == 0) {
        char params[64] = {};
        api->make_text(kTo, reinterpret_cast<ArkText*>(params + gInTextOffset));
        api->process_event(tb, gSetText, params);
        api->log("hello_menu: renamed \"%s\" -> \"%s\"", kFrom, kTo);
    }
    return 1;
}

void Tick(void*) {
    if (++gFrame % 30) return;  // twice a second is plenty
    UObject* world = api->get_world();
    if (world != gLastWorld) {
        char name[128];
        api->object_name(world, name, sizeof name);
        api->log("hello_menu: world -> %s (BeginPlay calls so far: %d)", name, gBeginPlays);
        gLastWorld = world;
    }
    api->for_each_object(gTextBlockClass, VisitTextBlock, nullptr);
}

int OnBeginPlay(ArkHookCtx*) {
    ++gBeginPlays;
    return 1;  // let the original run
}

}  // namespace

extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "Hello Menu"; }

extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) {
    api = a;
    if (a->version < 1) return 1;
    gTextBlockClass = a->find_object("/Script/UMG.TextBlock");
    gSetText = gTextBlockClass ? a->find_function(gTextBlockClass, "SetText") : nullptr;
    if (!gSetText) return 2;
    gTextOffset = a->property_offset(gTextBlockClass, "Text");
    gInTextOffset = a->property_offset(gSetText, "InText");
    gParamsSize = a->params_size(gSetText);
    a->log("hello_menu: Text@%d, SetText(InText@%d) params=%d", gTextOffset, gInTextOffset, gParamsSize);
    if (gTextOffset < 0 || gInTextOffset < 0 || gParamsSize > 64) return 3;

    a->hook_function("Actor.ReceiveBeginPlay", OnBeginPlay, nullptr, nullptr);
    a->on_tick(Tick, nullptr);
    return 0;
}
