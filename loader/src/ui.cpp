#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

#include "dispatch.h"
#include "engine.h"
#include "hook.h"
#include "log.h"
#include "lua_runtime.h"
#include "mods.h"

namespace ui {
namespace {

using engine::UObject;
using ue4::At;

// ------------------------------------------------------------------ HUD drawing via UFunctions
UObject *gDrawRectFn, *gDrawTextFn;
int oRectColor, oRectX, oRectY, oRectW, oRectH;
int oTxt, oTxtColor, oTxtX, oTxtY, oTxtFont, oTxtScale, oTxtScalePos;
int oCanvas = -1, oSizeX = -1, oSizeY = -1;
UObject* gHud = nullptr;  // set while drawing
float gW = 0, gH = 0;

// ------------------------------------------------------------------ registrations
struct Draw { ArkDrawFn fn; void* user; const void* owner; };
struct Touch { ArkTouchFn fn; void* user; const void* owner; };
struct Toggle { std::string label; int* value; const void* owner; };
std::mutex gMu;
std::vector<Draw> gDraws;
std::vector<Touch> gTouches;
std::vector<Toggle> gToggles;

// ------------------------------------------------------------------ panel state
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Hit(float px, float py) const { return w > 0 && px >= x && px <= x + w && py >= y && py <= y + h; }
};
bool gOpen = false;
Rect gModsBtn;
Rect gView;            // visible panel area (rows are scrolled inside it)
float gScroll = 0;     // pixels scrolled from the top of the content
float gContentH = 0;
struct HitRow { Rect r; std::string file; int* toggle; bool enable; };
std::vector<HitRow> gHitRows;  // rebuilt every frame the panel is drawn (screen coordinates)

constexpr int kMaxFinger = 16;
enum class FingerOwner { None, Panel, PanelDrag, Mod };
struct Finger {
    FingerOwner who = FingerOwner::None;
    Touch handler{};
    float downX = 0, downY = 0, startScroll = 0;
    bool dragging = false;
};
Finger gFingers[kMaxFinger];

float MaxScroll() { return std::max(0.f, gContentH - gView.h); }

// ------------------------------------------------------------------ drawing
void Panel() {
    float s = gH / 900.f;
    gModsBtn = {gW - 400 * s, 140 * s, 220 * s, 90 * s};
    DrawRect(gModsBtn.x, gModsBtn.y, gModsBtn.w, gModsBtn.h, 0.02f, 0.25f, 0.35f, gOpen ? 0.95f : 0.8f);
    DrawText(gOpen ? "MODS  X" : "MODS", gModsBtn.x + 30 * s, gModsBtn.y + 18 * s, 2.4f * s, 1, 1, 1);
    gHitRows.clear();
    if (!gOpen) { gView = Rect{}; return; }

    std::vector<ModRow> mods = mods::PanelRows();
    std::vector<Toggle> toggles;
    { std::lock_guard<std::mutex> l(gMu); toggles = gToggles; }

    const float w = 600 * s, headerH = 80 * s, rowH = 68 * s, gap = 8 * s;
    gView = {gModsBtn.x - w - 20 * s, 140 * s, w + 16 * s, gH - 40 * s - 140 * s};

    // Layout in content coordinates first, so we know the total height.
    struct Item { float y, h; const ModRow* mod; const Toggle* toggle; };
    std::vector<Item> items;
    float y = 0;
    for (const ModRow& m : mods) {
        items.push_back({y, headerH, &m, nullptr});
        y += headerH + gap;
        if (!m.loaded) continue;
        for (const Toggle& t : toggles)
            if (t.owner == m.owner) { items.push_back({y, rowH, nullptr, &t}); y += rowH + gap; }
    }
    gContentH = y;
    gScroll = std::min(std::max(gScroll, 0.f), MaxScroll());

    const float x = gView.x;
    for (const Item& it : items) {
        float sy = gView.y + it.y - gScroll;
        if (sy < gView.y - 1 || sy + it.h > gView.y + gView.h + 1) continue;  // only fully visible rows
        if (it.mod) {
            const ModRow& m = *it.mod;
            Rect r{x, sy, w, it.h};
            DrawRect(r.x, r.y, r.w, r.h, 0.06f, 0.16f, 0.22f, 0.92f);
            DrawText(m.name.c_str(), r.x + 22 * s, r.y + (m.note.empty() ? 18 : 8) * s, 2.2f * s, 1, 1, 1);
            if (!m.note.empty()) DrawText(m.note.c_str(), r.x + 22 * s, r.y + 48 * s, 1.2f * s, 1, 0.8f, 0.3f);
            bool on = m.enabled;
            DrawText(on ? "ON" : "OFF", r.x + w - 110 * s, r.y + 18 * s, 2.2f * s, on ? 0.4f : 1.f, on ? 1.f : 0.4f, 0.4f);
            gHitRows.push_back({r, m.file, nullptr, !on});
        } else {
            const Toggle& t = *it.toggle;
            Rect tr{x + 40 * s, sy, w - 40 * s, it.h};
            bool v = t.value && *t.value;
            DrawRect(tr.x, tr.y, tr.w, tr.h, v ? 0.05f : 0.1f, v ? 0.45f : 0.1f, v ? 0.15f : 0.12f, 0.88f);
            DrawText(t.label.c_str(), tr.x + 22 * s, tr.y + 16 * s, 1.9f * s, 1, 1, 1);
            DrawText(v ? "ON" : "OFF", tr.x + tr.w - 110 * s, tr.y + 16 * s, 1.9f * s, v ? 0.4f : 1.f, v ? 1.f : 0.4f, 0.4f);
            gHitRows.push_back({tr, "", t.value, false});
        }
    }
    if (MaxScroll() > 0) {  // scrollbar: drag the list to scroll
        float trackX = x + w + 6 * s, barH = std::max(40 * s, gView.h * gView.h / gContentH);
        float barY = gView.y + (gView.h - barH) * (gScroll / MaxScroll());
        DrawRect(trackX, gView.y, 8 * s, gView.h, 1, 1, 1, 0.15f);
        DrawRect(trackX, barY, 8 * s, barH, 1, 1, 1, 0.7f);
    }
}

void (*gOrigDrawHUD)(UObject*) = nullptr;
bool gLogged = false;

void HookedDrawHUD(UObject* hud) {
    gOrigDrawHUD(hud);
    if (oCanvas < 0) oCanvas = engine::PropertyOffset(hud, "Canvas");
    UObject* canvas = oCanvas >= 0 ? At<UObject*>(hud, oCanvas) : nullptr;
    if (!canvas) return;
    if (oSizeX < 0) { oSizeX = engine::PropertyOffset(canvas, "SizeX"); oSizeY = engine::PropertyOffset(canvas, "SizeY"); }
    gW = float(At<int32_t>(canvas, oSizeX));
    gH = float(At<int32_t>(canvas, oSizeY));
    if (!gLogged) { gLogged = true; LOG("ui: drawing on %.0fx%.0f", gW, gH); }

    gHud = hud;
    std::vector<Draw> draws;
    { std::lock_guard<std::mutex> l(gMu); draws = gDraws; }
    for (const Draw& d : draws) {
        bool alive;  // an earlier callback may have caused this one's owner to unload
        { std::lock_guard<std::mutex> l(gMu);
          alive = std::any_of(gDraws.begin(), gDraws.end(), [&](const Draw& x) { return x.fn == d.fn && x.user == d.user; }); }
        if (alive) d.fn(d.user, gW, gH);
    }
    Panel();  // on top of mod drawings
    gHud = nullptr;
}

// ------------------------------------------------------------------ touch
struct Vec2 { float x, y; };
bool (*gOrigTouchStarted)(void*, const void*, const Vec2&, int, int);
bool (*gOrigTouchMoved)(void*, const Vec2&, int, int);
bool (*gOrigTouchEnded)(void*, const Vec2&, int, int);

void ApplyEnable(void* p) {
    if (luart::AnyBusy()) { dispatch::Post(ApplyEnable, p, nullptr); return; }  // not while a script runs
    auto* q = static_cast<std::pair<std::string, bool>*>(p);
    mods::SetEnabled(q->first, q->second);
    delete q;
}

void ActivateRow(float x, float y) {
    for (const HitRow& r : gHitRows) {
        if (!r.r.Hit(x, y)) continue;
        if (r.toggle) *r.toggle = !*r.toggle;
        else dispatch::Post(ApplyEnable, new std::pair<std::string, bool>(r.file, r.enable), nullptr);
        return;  // (loading/unloading must not happen in the middle of input handling)
    }
}

bool HookedTouchStarted(void* app, const void* win, const Vec2& p, int idx, int ctrl) {
    if (gW > 0 && !engine::IsMainMenu()) {
        Finger scratch;
        Finger& f = idx >= 0 && idx < kMaxFinger ? gFingers[idx] : scratch;
        f = Finger{};
        if (gModsBtn.Hit(p.x, p.y)) { gOpen = !gOpen; f.who = FingerOwner::Panel; return true; }
        if (gOpen && gView.Hit(p.x, p.y)) {
            // Decide tap vs. scroll when the finger moves or lifts.
            f.who = FingerOwner::PanelDrag;
            f.downX = p.x; f.downY = p.y; f.startScroll = gScroll;
            return true;
        }
        std::vector<Touch> touches;
        { std::lock_guard<std::mutex> l(gMu); touches = gTouches; }
        for (const Touch& t : touches) {
            bool alive;
            { std::lock_guard<std::mutex> l(gMu);
              alive = std::any_of(gTouches.begin(), gTouches.end(), [&](const Touch& x) { return x.fn == t.fn && x.user == t.user; }); }
            if (alive && t.fn(t.user, 0, idx, p.x, p.y)) { f.who = FingerOwner::Mod; f.handler = t; return true; }
        }
    }
    return gOrigTouchStarted(app, win, p, idx, ctrl);
}

bool HookedTouchMoved(void* app, const Vec2& p, int idx, int ctrl) {
    if (idx >= 0 && idx < kMaxFinger && gFingers[idx].who != FingerOwner::None) {
        Finger& f = gFingers[idx];
        if (f.who == FingerOwner::Mod) f.handler.fn(f.handler.user, 1, idx, p.x, p.y);
        if (f.who == FingerOwner::PanelDrag) {
            float dy = p.y - f.downY;
            if (!f.dragging && std::abs(dy) > 14.f * gH / 900.f) f.dragging = true;
            if (f.dragging) gScroll = std::min(std::max(f.startScroll - dy, 0.f), MaxScroll());
        }
        return true;
    }
    return gOrigTouchMoved(app, p, idx, ctrl);
}

bool HookedTouchEnded(void* app, const Vec2& p, int idx, int ctrl) {
    if (idx >= 0 && idx < kMaxFinger && gFingers[idx].who != FingerOwner::None) {
        Finger& f = gFingers[idx];
        if (f.who == FingerOwner::Mod) f.handler.fn(f.handler.user, 2, idx, p.x, p.y);
        if (f.who == FingerOwner::PanelDrag && !f.dragging) ActivateRow(f.downX, f.downY);
        f = Finger{};
        return true;
    }
    return gOrigTouchEnded(app, p, idx, ctrl);
}

template <typename T>
bool Hook(T& orig, const char* sym, void* repl) {
    const char* err = "symbol not found";
    void* target = engine::Sym(sym);
    orig = reinterpret_cast<T>(target ? hook::InlineHook(target, repl, &err) : nullptr);
    if (!orig) LOGE("ui: cannot hook %s: %s", sym, err);
    return orig != nullptr;
}

int Param(UObject* fn, const char* name, bool& ok) {
    int o = engine::PropertyOffset(fn, name);
    if (o < 0) { LOGE("ui: missing param %s", name); ok = false; }
    return o;
}

}  // namespace

bool Init() {
    UObject* hud = engine::FindObject("/Script/Engine.HUD");
    gDrawRectFn = hud ? engine::FindField(hud, "DrawRect") : nullptr;
    gDrawTextFn = hud ? engine::FindField(hud, "DrawText") : nullptr;
    if (!gDrawRectFn || !gDrawTextFn) { LOGE("ui: HUD.DrawRect/DrawText not found"); return false; }
    bool ok = true;
    oRectColor = Param(gDrawRectFn, "RectColor", ok); oRectX = Param(gDrawRectFn, "ScreenX", ok);
    oRectY = Param(gDrawRectFn, "ScreenY", ok); oRectW = Param(gDrawRectFn, "ScreenW", ok);
    oRectH = Param(gDrawRectFn, "ScreenH", ok);
    oTxt = Param(gDrawTextFn, "Text", ok); oTxtColor = Param(gDrawTextFn, "TextColor", ok);
    oTxtX = Param(gDrawTextFn, "ScreenX", ok); oTxtY = Param(gDrawTextFn, "ScreenY", ok);
    oTxtFont = Param(gDrawTextFn, "Font", ok); oTxtScale = Param(gDrawTextFn, "Scale", ok);
    oTxtScalePos = Param(gDrawTextFn, "bScalePosition", ok);
    if (!ok || engine::ParamsSize(gDrawRectFn) > 64 || engine::ParamsSize(gDrawTextFn) > 96) return false;

    ok &= Hook(gOrigDrawHUD, "_ZN11AShooterHUD7DrawHUDEv", reinterpret_cast<void*>(&HookedDrawHUD));
    ok &= Hook(gOrigTouchStarted,
               "_ZN17FSlateApplication14OnTouchStartedERK10TSharedPtrI14FGenericWindowL7ESPMode0EERK9FVector2Dii",
               reinterpret_cast<void*>(&HookedTouchStarted));
    ok &= Hook(gOrigTouchMoved, "_ZN17FSlateApplication12OnTouchMovedERK9FVector2Dii",
               reinterpret_cast<void*>(&HookedTouchMoved));
    ok &= Hook(gOrigTouchEnded, "_ZN17FSlateApplication12OnTouchEndedERK9FVector2Dii",
               reinterpret_cast<void*>(&HookedTouchEnded));
    if (ok) LOG("ui: MODS panel ready");
    return ok;
}

void DrawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    if (!gHud) return;
    char p[64] = {};
    float* c = reinterpret_cast<float*>(p + oRectColor);
    c[0] = r; c[1] = g; c[2] = b; c[3] = a;
    At<float>(p, oRectX) = x; At<float>(p, oRectY) = y; At<float>(p, oRectW) = w; At<float>(p, oRectH) = h;
    dispatch::CallOriginal(gHud, gDrawRectFn, p);
}

void DrawText(const char* s, float x, float y, float scale, float r, float g, float b) {
    if (!gHud || !s) return;
    wchar_t w[128];
    int n = 0;
    for (; s[n] && n < 127; ++n) w[n] = wchar_t(static_cast<unsigned char>(s[n]));
    w[n] = 0;
    char p[96] = {};
    ue4::FString str;
    str.Data = w; str.Num = str.Max = n + 1;
    std::memcpy(p + oTxt, &str, sizeof str);
    float* c = reinterpret_cast<float*>(p + oTxtColor);
    c[0] = r; c[1] = g; c[2] = b; c[3] = 1;
    At<float>(p, oTxtX) = x; At<float>(p, oTxtY) = y;
    At<void*>(p, oTxtFont) = nullptr;  // engine medium font
    At<float>(p, oTxtScale) = scale;
    At<bool>(p, oTxtScalePos) = false;
    dispatch::CallOriginal(gHud, gDrawTextFn, p);
}

void AddDraw(ArkDrawFn fn, void* user, const void* owner) {
    std::lock_guard<std::mutex> l(gMu);
    gDraws.push_back({fn, user, owner});
}
void AddTouch(ArkTouchFn fn, void* user, const void* owner) {
    std::lock_guard<std::mutex> l(gMu);
    gTouches.push_back({fn, user, owner});
}
void AddToggle(const char* label, int* value, const void* owner) {
    std::lock_guard<std::mutex> l(gMu);
    gToggles.push_back({label ? label : "?", value, owner});
}

void RemoveOwner(const void* owner) {
    std::lock_guard<std::mutex> l(gMu);
    gDraws.erase(std::remove_if(gDraws.begin(), gDraws.end(), [&](const Draw& d) { return d.owner == owner; }), gDraws.end());
    gTouches.erase(std::remove_if(gTouches.begin(), gTouches.end(), [&](const Touch& t) { return t.owner == owner; }),
                   gTouches.end());
    gToggles.erase(std::remove_if(gToggles.begin(), gToggles.end(), [&](const Toggle& t) { return t.owner == owner; }),
                   gToggles.end());
    for (Finger& f : gFingers)
        if (f.who == FingerOwner::Mod && f.handler.owner == owner) f.who = FingerOwner::Panel;  // swallow the rest
}

}  // namespace ui
