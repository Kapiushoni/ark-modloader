// Example mod: renames the main-menu "PLAY ARK" button.
// Setting (ArkMods/hello_menu.ini): button_text — edit it while the game runs.
// On unload the original text is restored.
#include <arkmod.h>

#include <cstring>
#include <string>

namespace {

const ArkModAPI* api;
UObject* gTextBlockClass;
UObject* gSetText;
int gTextOffset = -1, gInTextOffset = -1;
int gFrame = 0;

const char* kOriginal = "PLAY ARK";
std::string gWanted;   // text we want on the button now
std::string gApplied;  // text we put there last time (to recognise our own label)

void SetText(UObject* tb, const char* text) {
    char params[64] = {};
    api->make_text(text, reinterpret_cast<ArkText*>(params + gInTextOffset));
    api->process_event(tb, gSetText, params);
}

struct Rename { const char* from1; const char* from2; const char* to; int count; };

int Visit(UObject* tb, void* user) {
    auto* r = static_cast<Rename*>(user);
    char text[256];
    api->text_to_utf8(reinterpret_cast<ArkText*>(reinterpret_cast<char*>(tb) + gTextOffset), text, sizeof text);
    if ((r->from1 && !strcmp(text, r->from1)) || (r->from2 && !strcmp(text, r->from2))) {
        if (strcmp(text, r->to)) { SetText(tb, r->to); ++r->count; }
    }
    return 1;
}

void Tick(void*) {
    if (++gFrame % 30) return;  // twice a second is plenty
    gWanted = api->config_string("button_text", "PLAY ARK (MODDED)");
    Rename r{kOriginal, gApplied.empty() ? nullptr : gApplied.c_str(), gWanted.c_str(), 0};
    api->for_each_object(gTextBlockClass, Visit, &r);
    if (r.count) api->log("hello_menu: button text -> \"%s\"", gWanted.c_str());
    gApplied = gWanted;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "Hello Menu"; }

extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* a) {
    api = a;
    if (a->version < 2) return 1;
    gTextBlockClass = a->find_object("/Script/UMG.TextBlock");
    gSetText = gTextBlockClass ? a->find_function(gTextBlockClass, "SetText") : nullptr;
    if (!gSetText) return 2;
    gTextOffset = a->property_offset(gTextBlockClass, "Text");
    gInTextOffset = a->property_offset(gSetText, "InText");
    if (gTextOffset < 0 || gInTextOffset < 0 || a->params_size(gSetText) > 64) return 3;
    a->on_tick(Tick, nullptr);
    return 0;
}

extern "C" __attribute__((visibility("default"))) void ArkMod_Shutdown() {
    if (gApplied.empty()) return;
    Rename r{gApplied.c_str(), nullptr, kOriginal, 0};
    api->for_each_object(gTextBlockClass, Visit, &r);
    api->log("hello_menu: restored \"%s\" on %d widget(s)", kOriginal, r.count);
}
