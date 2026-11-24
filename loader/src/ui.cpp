#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <map>
#include <mutex>

#include "dispatch.h"
#include "engine.h"
#include "hook.h"
#include "i18n.h"
#include "log.h"
#include "lua_runtime.h"
#include "mods.h"
#include "sdk.h"
#include "umg.h"

namespace ui {
namespace {

using engine::UObject;
using ue4::At;

// ------------------------------------------------------------------ HUD drawing for mods (UFunctions)
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

// ------------------------------------------------------------------ overlay (UMG widgets)
// Overlay tree on the game viewport (rooted, shown again after every map load):
//   gRoot CanvasPanel
//     gGameBtn  "MODS" button, top centre, in game only (in the main menu a copy of EXIT does it)
//     gPanel    the MODS panel: a Frame (see BuildFrame) listing mods, their toggles and windows
//     mod windows: one Frame each, built on first show
//     gStyleBtn, gStyleText, gStyleScroll  hidden copies of the main menu's EXIT button, its label and the
//                patch-notes scroll box: the overlay reuses their look
// Only one frame is open at a time (the panel or one window).
UObject *gRoot, *gGameBtn, *gStyleBtn, *gStyleText, *gStyleScroll;
UObject* gShownWorld = nullptr;
bool gUmgReady = false;
int gGameBtnShown = -1;  // last visibility given to gGameBtn

UObject* gMenuExit = nullptr;  // live main-menu EXIT button we copied
UObject* gMenuMods = nullptr;  // our MODS copy of it
std::vector<std::pair<UObject*, UObject*>> gMenuMirror;  // (copy, original): menu animations move the originals

struct Action {
    enum Kind { Nothing, TogglePanel, ClosePanel, EnableMod, FlipToggle, OpenWindow, CloseWindow,
                Click, Flip, Step, OpenSettings, DumpSdk } kind = Nothing;
    std::string file;
    bool enable = false;
    int* value = nullptr;
    std::string label;
    const void* owner = nullptr;
    int id = 0;     // window / control id
    float dir = 0;  // Step: -1 / +1
};
std::map<UObject*, Action> gActions;  // button -> what a tap on it does
std::vector<Action> gPending;         // taps, applied on the next tick (never inside Slate's click)

constexpr float kPanelWidth = 820, kListWidth = kPanelWidth - 2 * 24 - 12;  // minus border padding and scroll bar
const float kOn[4] = {0.45f, 1.f, 0.45f, 1}, kOff[4] = {1.f, 0.45f, 0.45f, 1}, kLink[4] = {0.55f, 0.85f, 1.f, 1};

UObject* NewText(const char* text, int size) {
    UObject* t = umg::New(umg::Class("TextBlock"), gStyleText);
    umg::SetVisibility(t, umg::Vis::HitTestInvisible);  // the style copy is hidden; taps go to the button
    umg::SetFontSize(t, size);
    umg::SetText(t, text);
    return t;
}

UObject* NewButton() {
    UObject* b = umg::New(umg::Class("Button"), gStyleBtn);
    umg::SetVisibility(b, umg::Vis::Visible);  // the style copy is hidden
    umg::SetPreciseTap(b);
    return b;
}

// A centred, closable window: invisible Button (swallows taps between rows) -> Border ->
// VerticalBox [ header: title + X ] [ scroll box -> list ]. Added to gRoot hidden.
struct Frame { UObject *host = nullptr, *scroll = nullptr, *title = nullptr, *close = nullptr; };

Frame BuildFrame(const char* title) {
    using namespace umg;
    Frame f;
    f.host = New(Class("Button"));
    SetButtonColor(f.host, 0, 0, 0, 0);
    UObject* border = New(Class("Border"));
    SetBorderColor(border, 0.01f, 0.03f, 0.05f, 0.92f);
    SetBorderPadding(border, 24, 16, 24, 24);
    UObject* box = New(Class("VerticalBox"));

    UObject* header = New(Class("HorizontalBox"));
    f.title = NewText(title, 40);
    UObject* s = AddChild(header, f.title);
    SetFill(s, 1);
    SetAlign(s, HLeft, VCenter);
    f.close = NewButton();
    AddChild(f.close, NewText("  X  ", 34));
    if (!gStyleBtn) SetButtonColor(f.close, 0.35f, 0.1f, 0.1f, 1);
    SetAlign(AddChild(header, f.close), HRight, VCenter);
    SetPadding(AddChild(box, header), 0, 0, 0, 12);

    // UMG's ScrollBox lays its content out with no width in this build; ARK's ScrollBoxAutoSize works.
    f.scroll = gStyleScroll ? New(reinterpret_cast<UObject*>(gStyleScroll->ClassPrivate), gStyleScroll)
                            : New(Class("ScrollBox"));
    SetVisibility(f.scroll, Vis::Visible);
    SetFill(AddChild(box, f.scroll), 1);
    AddChild(border, box);
    s = AddChild(f.host, border);
    SetPadding(s, 0, 0, 0, 0);
    SetAlign(s, HFill, VFill);
    SetCanvasLayout(AddChild(gRoot, f.host), 0.5f, 0, 0.5f, 1, 0, 40, kPanelWidth, 40, 0.5f, 0);
    SetVisibility(f.host, Vis::Collapsed);
    return f;
}

// Puts a fully built list into a frame. Lists are always built first and attached last: widgets
// added to a scroll box's content after it is on screen get no width in this engine build.
void SetList(const Frame& f, UObject* list) {
    umg::ClearChildren(f.scroll);
    UObject* width = umg::New(umg::Class("SizeBox"));  // the scroll box sizes content to its own width
    umg::Call1(width, "SetMinDesiredWidth", kListWidth);
    umg::AddChild(width, list);
    umg::AddChild(f.scroll, width);
}

// One tappable row: [ name (+ note) ........ STATE ]
struct Row { UObject *button = nullptr, *state = nullptr; std::string shown; };

void SetState(Row& r, const char* state, const float* color) {
    if (r.shown == state) return;
    umg::SetText(r.state, state);
    umg::SetTextColor(r.state, color[0], color[1], color[2], color[3]);
    r.shown = state;
}
void SetOnOff(Row& r, bool on) {
    SetState(r, i18n::Tr(on ? "ON" : "OFF").c_str(), on ? kOn : kOff);
    if (!gStyleBtn) umg::SetButtonColor(r.button, on ? 0.15f : 0.2f, on ? 0.45f : 0.2f, on ? 0.25f : 0.22f, 1);
}

Row AddRow(UObject* list, const std::string& name, const std::string& note, bool big, float indent) {
    Row r;
    r.button = NewButton();
    UObject* line = umg::New(umg::Class("HorizontalBox"));
    UObject* texts = umg::New(umg::Class("VerticalBox"));
    umg::AddChild(texts, NewText(name.c_str(), big ? 30 : 24));
    if (!note.empty()) {
        UObject* n = NewText(note.c_str(), 16);
        umg::SetTextColor(n, 1, 0.8f, 0.3f);
        umg::AddChild(texts, n);
    }
    UObject* s = umg::AddChild(line, texts);
    umg::SetFill(s, 1);
    umg::SetAlign(s, umg::HLeft, umg::VCenter);
    // Fixed-width state column, text left-aligned in it: Cyrillic is drawn with a fallback font that
    // is wider than the engine measures, so right-aligned text would run past the row's edge.
    r.state = NewText("", big ? 30 : 24);
    UObject* cell = umg::New(umg::Class("SizeBox"));
    umg::Call1(cell, "SetWidthOverride", 240.f);
    umg::SetAlign(umg::AddChild(cell, r.state), umg::HLeft, umg::VCenter);
    s = umg::AddChild(line, cell);
    umg::SetAlign(s, umg::HRight, umg::VCenter);
    umg::SetPadding(s, 24, 0, 0, 0);
    s = umg::AddChild(r.button, line);
    umg::SetAlign(s, umg::HFill, umg::VCenter);
    umg::SetPadding(s, 24, 10, 24, 10);
    s = umg::AddChild(list, r.button);
    umg::SetPadding(s, indent, big ? 10 : 4, 8, 0);
    return r;
}

// ------------------------------------------------------------------ mod windows (model)
struct Control {
    enum Kind { Label, Button, Check, Number } kind = Label;
    int window = 0;
    std::string text;
    ArkUiFn click = nullptr;
    ArkUiChangedFn changed = nullptr;
    void* user = nullptr;
    int* check = nullptr;
    float* number = nullptr;
    float min = 0, max = 0, step = 1;
    // widgets (game thread)
    UObject* textW = nullptr;
    std::string shownText;
    Row row;
};
struct Window {
    std::string title;
    const void* owner = nullptr;
    std::vector<int> controls;
    int version = 0;  // bumped when controls are added
    bool open = false;
    bool listed = true;  // shown as "OPEN >" under its mod (not the loader's SETTINGS windows)
    // widgets (game thread)
    Frame frame;
    int builtVersion = -1;
    bool shown = false;
    std::string shownTitle;
};
std::map<int, Window> gWindows;   // under gMu
std::map<int, Control> gControls; // under gMu
int gNextId = 1;
std::vector<UObject*> gDeadFrames;  // frames of removed windows, taken off screen on the next tick
bool gClosePanel = false;           // a window was opened (maybe off the game thread): close the panel

// ------------------------------------------------------------------ MODS panel
Frame gPanel;
bool gOpen = false;  // the panel
std::vector<Row> gRows;
struct RowKind { bool modRow; int* value; int window; };
std::vector<RowKind> gRowKinds;
std::string gRowsKey;
Row gSdkRow;  // "DUMP SDK" at the end of the panel

bool AnyOpen() {
    if (gOpen) return true;
    std::lock_guard<std::mutex> l(gMu);
    for (auto& w : gWindows) if (w.second.open) return true;
    return false;
}

void RefreshRows() {
    std::vector<ModRow> mods = mods::PanelRows();
    std::vector<Toggle> toggles;
    std::vector<std::pair<int, Window*>> windows;
    std::lock_guard<std::mutex> l(gMu);
    toggles = gToggles;
    for (auto& w : gWindows) windows.push_back({w.first, &w.second});

    std::string key;  // rebuild only when the list itself changes
    for (const ModRow& m : mods) {
        key += m.file + '|' + m.name + '|' + m.note + '|' + char('0' + m.loaded) + '\n';
        if (!m.loaded) continue;
        for (const Toggle& t : toggles)
            if (t.owner == m.owner) key += "  " + t.label + '\n';
        for (auto& w : windows)
            if (w.second->owner == m.owner && w.second->listed)
                key += "  [" + std::to_string(w.first) + "]" + w.second->title + '\n';
        if (m.hasSettings) key += "  settings\n";
    }
    if (key != gRowsKey) {
        gRowsKey = key;
        for (const Row& r : gRows) gActions.erase(r.button);
        gRows.clear();
        gRowKinds.clear();
        UObject* list = umg::New(umg::Class("VerticalBox"));
        for (const ModRow& m : mods) {
            Row r = AddRow(list, m.name, i18n::Tr(m.note), true, 0);
            SetOnOff(r, m.enabled);
            gActions[r.button] = Action{Action::EnableMod, m.file, !m.enabled};
            gRows.push_back(r);
            gRowKinds.push_back({true, nullptr, 0});
            if (!m.loaded) continue;
            for (const Toggle& t : toggles) {
                if (t.owner != m.owner) continue;
                Row tr = AddRow(list, t.label, "", false, 56);
                SetOnOff(tr, t.value && *t.value);
                gActions[tr.button] = Action{Action::FlipToggle, "", false, t.value, t.label, t.owner};
                gRows.push_back(tr);
                gRowKinds.push_back({false, t.value, 0});
            }
            for (auto& w : windows) {
                if (w.second->owner != m.owner || !w.second->listed) continue;
                Row wr = AddRow(list, w.second->title, "", false, 56);
                SetState(wr, i18n::Tr("OPEN >").c_str(), kLink);
                Action a{Action::OpenWindow};
                a.id = w.first;
                gActions[wr.button] = a;
                gRows.push_back(wr);
                gRowKinds.push_back({false, nullptr, w.first});
            }
            if (m.hasSettings) {
                Row sr = AddRow(list, i18n::Tr("SETTINGS"), "", false, 56);
                SetState(sr, i18n::Tr("OPEN >").c_str(), kLink);
                Action a{Action::OpenSettings, "", false, nullptr, m.name, m.owner};
                gActions[sr.button] = a;
                gRows.push_back(sr);
                gRowKinds.push_back({false, nullptr, -1});
            }
        }
        if (mods.empty()) umg::AddChild(list, NewText(i18n::Tr("No mods in the ArkMods folder").c_str(), 24));
        // Loader tools.
        gSdkRow = AddRow(list, i18n::Tr("DUMP SDK"), i18n::Tr("all classes, fields and functions -> ArkMods/sdk/sdk.txt"), true, 0);
        SetState(gSdkRow, i18n::Tr("RUN >").c_str(), kLink);
        gActions[gSdkRow.button] = Action{Action::DumpSdk};
        SetList(gPanel, list);
        return;
    }
    size_t mi = 0;  // same list: just follow the values
    for (size_t i = 0; i < gRows.size(); ++i) {
        const RowKind& k = gRowKinds[i];
        if (k.window) continue;
        bool on = k.modRow ? (mi < mods.size() && mods[mi++].enabled) : (k.value && *k.value);
        if (k.modRow) gActions[gRows[i].button].enable = !on;
        SetOnOff(gRows[i], on);
    }
}

void CloseWindows() {
    std::lock_guard<std::mutex> l(gMu);
    for (auto& w : gWindows) w.second.open = false;
}

void SetOpen(bool open) {
    if (open) CloseWindows();
    gOpen = open;
    if (!gPanel.host) return;
    umg::SetVisibility(gPanel.host, open ? umg::Vis::Visible : umg::Vis::Collapsed);
    if (open) RefreshRows();
}

// ------------------------------------------------------------------ mod windows (widgets)
std::string FormatNumber(float v, float step) {
    int decimals = 0;
    for (float s = step; s > 0 && s < 0.999f && decimals < 4; s *= 10) ++decimals;
    char buf[32];
    snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

UObject* SmallButton(const char* text, Action a) {
    UObject* b = NewButton();
    UObject* s = umg::AddChild(b, NewText(text, 30));
    umg::SetAlign(s, umg::HCenter, umg::VCenter);
    umg::SetPadding(s, 22, 6, 22, 6);
    gActions[b] = a;
    return b;
}

void BuildWindowList(int id, Window& w) {  // gMu held
    UObject* list = umg::New(umg::Class("VerticalBox"));
    for (int cid : w.controls) {
        auto it = gControls.find(cid);
        if (it == gControls.end()) continue;
        Control& c = it->second;
        Action a;
        a.id = cid;
        c.shownText = c.text;
        c.row = Row{};
        switch (c.kind) {
            case Control::Label: {
                c.textW = NewText(c.text.c_str(), 24);
                umg::SetPadding(umg::AddChild(list, c.textW), 8, 10, 8, 2);
                break;
            }
            case Control::Button: {
                UObject* b = NewButton();
                c.textW = NewText(c.text.c_str(), 28);
                UObject* s = umg::AddChild(b, c.textW);
                umg::SetAlign(s, umg::HCenter, umg::VCenter);
                umg::SetPadding(s, 24, 10, 24, 10);
                umg::SetPadding(umg::AddChild(list, b), 0, 10, 8, 0);
                a.kind = Action::Click;
                gActions[b] = a;
                break;
            }
            case Control::Check: {
                c.row = AddRow(list, c.text, "", false, 0);
                SetOnOff(c.row, c.check && *c.check);
                a.kind = Action::Flip;
                gActions[c.row.button] = a;
                break;
            }
            case Control::Number: {  // [ label ......  -  value  + ] on a row-styled background
                UObject* bg = NewButton();
                UObject* line = umg::New(umg::Class("HorizontalBox"));
                c.textW = NewText(c.text.c_str(), 24);
                UObject* s = umg::AddChild(line, c.textW);
                umg::SetFill(s, 1);
                umg::SetAlign(s, umg::HLeft, umg::VCenter);
                a.kind = Action::Step;
                a.dir = -1;
                umg::SetAlign(umg::AddChild(line, SmallButton("-", a)), umg::HRight, umg::VCenter);
                c.row.state = NewText(FormatNumber(c.number ? *c.number : 0, c.step).c_str(), 26);
                c.row.shown = "";
                UObject* cell = umg::New(umg::Class("SizeBox"));  // fixed width: the -/+ line up row to row
                umg::Call1(cell, "SetWidthOverride", 150.f);
                umg::SetAlign(umg::AddChild(cell, c.row.state), umg::HCenter, umg::VCenter);
                s = umg::AddChild(line, cell);
                umg::SetAlign(s, umg::HCenter, umg::VCenter);
                a.dir = 1;
                umg::SetAlign(umg::AddChild(line, SmallButton("+", a)), umg::HRight, umg::VCenter);
                s = umg::AddChild(bg, line);
                umg::SetAlign(s, umg::HFill, umg::VCenter);
                umg::SetPadding(s, 24, 4, 8, 4);
                umg::SetPadding(umg::AddChild(list, bg), 0, 4, 8, 0);
                c.row.button = bg;  // a tap on the background itself does nothing
                break;
            }
        }
    }
    if (w.controls.empty()) umg::AddChild(list, NewText(i18n::Tr("(empty)").c_str(), 24));
    SetList(w.frame, list);
    w.builtVersion = w.version;
    (void)id;
}

void RefreshWindowValues(Window& w) {  // gMu held
    if (w.shownTitle != w.title) { umg::SetText(w.frame.title, w.title.c_str()); w.shownTitle = w.title; }
    for (int cid : w.controls) {
        auto it = gControls.find(cid);
        if (it == gControls.end()) continue;
        Control& c = it->second;
        if (c.textW && c.shownText != c.text) { umg::SetText(c.textW, c.text.c_str()); c.shownText = c.text; }
        if (c.kind == Control::Check && c.row.button) SetOnOff(c.row, c.check && *c.check);
        if (c.kind == Control::Number && c.row.state) {
            std::string v = FormatNumber(c.number ? *c.number : 0, c.step);
            if (v != c.row.shown) { umg::SetText(c.row.state, v.c_str()); c.row.shown = v; }
        }
    }
}

void UpdateWindows(bool refreshValues) {
    std::vector<UObject*> dead;
    std::lock_guard<std::mutex> l(gMu);
    dead.swap(gDeadFrames);
    for (UObject* host : dead) {
        if (!engine::IsValid(host)) continue;
        umg::SetVisibility(host, umg::Vis::Collapsed);
        umg::Call1(host, "RemoveFromParent", uint8_t(0));
    }
    for (auto& e : gWindows) {
        Window& w = e.second;
        if (w.open && !w.frame.host) {
            w.frame = BuildFrame(w.title.c_str());
            w.shownTitle = w.title;
            Action close{Action::CloseWindow};
            close.id = e.first;
            gActions[w.frame.close] = close;
        }
        if (w.open && w.builtVersion != w.version) BuildWindowList(e.first, w);
        if (w.frame.host && w.open != w.shown) {
            umg::SetVisibility(w.frame.host, w.open ? umg::Vis::Visible : umg::Vis::Collapsed);
            w.shown = w.open;
        }
        if (w.open && refreshValues) RefreshWindowValues(w);
    }
}

void BuildOverlay() {
    using namespace umg;
    gRows.clear();
    gRowKinds.clear();
    gRowsKey = "\x01";  // force a rebuild into the new list
    gRoot = New(Class("CanvasPanel"), nullptr, nullptr, /*root=*/true);
    if (!gRoot) return;
    SetVisibility(gRoot, Vis::SelfHitTestInvisible);  // empty space passes touches to the game

    gGameBtn = NewButton();
    AddChild(gGameBtn, NewText(i18n::Tr("MODS").c_str(), 34));
    if (!gStyleBtn) SetButtonColor(gGameBtn, 0.02f, 0.25f, 0.35f, 0.9f);
    // top centre: the right edge holds ARK's notices
    SetCanvasLayout(AddChild(gRoot, gGameBtn), 0.5f, 0, 0.5f, 0, 0, 12, 220, 84, 0.5f, 0);
    gActions[gGameBtn] = Action{Action::TogglePanel};

    gPanel = BuildFrame(i18n::Tr("MODS").c_str());
    gActions[gPanel.close] = Action{Action::ClosePanel};
    for (UObject* style : {gStyleBtn, gStyleText, gStyleScroll}) {
        if (!style) continue;
        AddChild(gRoot, style);  // keeps the copies (and the textures they use) alive
        SetVisibility(style, Vis::Collapsed);
    }
    {
        std::lock_guard<std::mutex> l(gMu);  // windows get rebuilt in the new tree when shown
        for (auto& w : gWindows) { w.second.frame = Frame{}; w.second.builtVersion = -1; w.second.shown = false; }
    }
    gOpen = false;
    gGameBtnShown = -1;
}

// ---- main menu: a MODS entry right under EXIT, copied from it
// A widget of the live main menu (not the menu class's template tree, which uses the same names).
UObject* FindMenuWidget(const char* cls, const char* name) {
    static UObject* classClass = engine::FindObject("/Script/CoreUObject.Class");
    UObject* wanted = cls ? umg::Class(cls) : nullptr;  // nullptr: any class
    ue4::FFixedUObjectArray& arr = engine::Objects()->ObjObjects;
    for (int i = 0; i < arr.NumElements; ++i) {
        UObject* o = arr.Objects[i].Object;
        if (!o || (wanted && reinterpret_cast<UObject*>(o->ClassPrivate) != wanted) || engine::Name(o) != name) continue;
        bool archetype = false;
        for (UObject* out = o->OuterPrivate; out && !archetype; out = out->OuterPrivate)
            archetype = engine::IsA(out, classClass) || engine::IsCDO(out);
        if (!archetype && engine::Path(o).find("MainMenuUI_C") != std::string::npos) return o;
    }
    return nullptr;
}

UObject* FirstChild(UObject* panel) { return umg::ChildCount(panel) > 0 ? umg::ChildAt(panel, 0) : nullptr; }

void AddMenuEntry() {
    UObject* exitBtn = FindMenuWidget("Button", "ExitButton");
    if (!exitBtn) return;
    if (exitBtn == gMenuExit && engine::IsValid(gMenuMods) && umg::ParentOf(gMenuMods)) return;  // already there
    gMenuExit = exitBtn;
    UObject* exitPanel = umg::ParentOf(exitBtn);  // CanvasPanel ExitPanel
    UObject* list = umg::ParentOf(exitPanel);     // VerticalBox ButtonPanel
    UObject* scale = FirstChild(exitBtn);         // ScaleBox
    UObject* text = scale ? FirstChild(scale) : nullptr;
    if (!exitPanel || !list || !scale || !text) { LOGE("ui: main menu layout not as expected"); return; }

    UObject* tree = exitBtn->OuterPrivate;  // the menu's WidgetTree
    UObject* p = umg::New(umg::Class("CanvasPanel"), exitPanel, tree);
    UObject* b = umg::New(umg::Class("Button"), exitBtn, tree);
    UObject* sb = umg::New(umg::Class("ScaleBox"), scale, tree);
    UObject* tb = umg::New(umg::Class("TextBlock"), text, tree);
    if (!p || !b || !sb || !tb) { LOGE("ui: cannot copy the EXIT button"); return; }
    umg::SetText(tb, i18n::Tr("MODS").c_str());
    umg::CopySlot(umg::AddChild(sb, tb), umg::SlotOf(text));
    umg::CopySlot(umg::AddChild(b, sb), umg::SlotOf(scale));
    umg::CopySlot(umg::AddChild(p, b), umg::SlotOf(exitBtn));
    umg::CopySlot(umg::AddChild(list, p), umg::SlotOf(exitPanel));
    int n = umg::ChildCount(list);  // the list has a fixed height: make room for one more row
    if (n > 1) umg::ScaleCanvasHeight(umg::SlotOf(list), float(n) / float(n - 1));
    gActions.erase(gMenuMods);
    gActions[b] = Action{Action::TogglePanel};
    gMenuMods = b;
    gMenuMirror = {{p, exitPanel}, {b, exitBtn}, {sb, scale}, {tb, text}};

    if (!gStyleBtn) {  // first sight of the menu: remember its look for the overlay
        gStyleBtn = umg::New(umg::Class("Button"), exitBtn);
        gStyleText = umg::New(umg::Class("TextBlock"), text);
        if (UObject* sb = FindMenuWidget(nullptr, "ScrollBoxAutoSize_0"))  // the patch notes list
            gStyleScroll = umg::New(reinterpret_cast<UObject*>(sb->ClassPrivate), sb);
        LOG("ui: scroll box class %s", gStyleScroll ? engine::Name(reinterpret_cast<UObject*>(gStyleScroll->ClassPrivate)).c_str() : "none");
        if (gRoot) {  // rebuild the overlay with that look (the old tree just stays hidden)
            umg::HideFromViewport(gRoot);
            gActions.clear();
            gActions[b] = Action{Action::TogglePanel};
            gRoot = nullptr;
            gShownWorld = nullptr;
        }
    }
    LOG("ui: MODS entry added under EXIT in the main menu");
}

void ApplyEnable(void* p) {
    if (luart::AnyBusy()) { dispatch::Post(ApplyEnable, p, nullptr); return; }  // not while a script runs
    auto* q = static_cast<std::pair<std::string, bool>*>(p);
    mods::SetEnabled(q->first, q->second);
    delete q;
}

// A control action: change the value under the lock, call the mod outside it.
void ApplyControl(const Action& a) {
    ArkUiFn click = nullptr;
    ArkUiChangedFn changed = nullptr;
    void* user = nullptr;
    float value = 0;
    {
        std::lock_guard<std::mutex> l(gMu);
        auto it = gControls.find(a.id);
        if (it == gControls.end()) return;  // its mod was unloaded meanwhile
        Control& c = it->second;
        user = c.user;
        if (a.kind == Action::Click) click = c.click;
        if (a.kind == Action::Flip && c.check) {
            *c.check = !*c.check;
            value = float(*c.check);
            changed = c.changed;
            SetOnOff(c.row, *c.check);
        }
        if (a.kind == Action::Step && c.number) {
            float v = *c.number + a.dir * c.step;
            v = std::min(std::max(v, c.min), c.max);
            if (v == *c.number) return;
            *c.number = value = v;
            changed = c.changed;
        }
    }
    if (click) click(user);
    if (changed) changed(user, value);
}

// ------------------------------------------------------------------ SETTINGS windows
// Built by the loader from a mod's ini each time they are opened; every change is written back
// at once (mods read their settings live).
struct SettingEntry {
    const void* owner = nullptr;
    std::string key, onStr, offStr;
    bool isBool = false;
    int on = 0;
    float value = 0, step = 1;
};
std::map<int, std::vector<std::unique_ptr<SettingEntry>>> gSettingStore;  // window -> its entries (gMu)
std::map<const void*, int> gSettingsWin;                                // mod -> its SETTINGS window (gMu)

void DropWindowLocked(int id) {  // gMu held
    auto w = gWindows.find(id);
    if (w == gWindows.end()) return;
    if (w->second.frame.host) gDeadFrames.push_back(w->second.frame.host);
    for (int cid : w->second.controls) gControls.erase(cid);
    gWindows.erase(w);
    gSettingStore.erase(id);
    for (auto it = gSettingsWin.begin(); it != gSettingsWin.end();)
        it = it->second == id ? gSettingsWin.erase(it) : std::next(it);
}

void SettingChanged(void* user, float v) {
    auto* e = static_cast<SettingEntry*>(user);
    mods::SaveSetting(e->owner, e->key, e->isBool ? (v != 0 ? e->onStr : e->offStr) : FormatNumber(v, e->step));
}

std::string Pretty(std::string key) {
    for (char& c : key) c = (c == '_' || c == '.') ? ' ' : char(toupper(static_cast<unsigned char>(c)));
    return key;
}

float GuessStep(const std::string& text, float v) {
    size_t dot = text.find('.');
    int decimals = 0;
    if (dot != std::string::npos)
        while (dot + 1 + decimals < text.size() && isdigit(static_cast<unsigned char>(text[dot + 1 + decimals]))) ++decimals;
    float a = std::fabs(v);
    if (a >= 100) return std::pow(10.f, std::floor(std::log10(a)) - 1);
    if (a >= 10) return 1;
    return decimals ? std::pow(10.f, -float(std::min(decimals, 2))) : 1;
}

void OpenSettingsWindow(const void* owner, const std::string& modName) {
    {
        std::lock_guard<std::mutex> l(gMu);
        auto it = gSettingsWin.find(owner);
        if (it != gSettingsWin.end()) DropWindowLocked(it->second);  // rebuilt: the file may have changed
    }
    std::vector<Setting> settings = mods::Settings(owner);
    int id = NewWindow((modName + " " + i18n::Tr("SETTINGS")).c_str(), owner);
    std::vector<std::unique_ptr<SettingEntry>> entries;
    bool text = false;
    for (const Setting& s : settings) {
        auto e = std::make_unique<SettingEntry>();
        e->owner = owner;
        e->key = s.key;
        std::string lv = s.value;
        for (char& c : lv) c = char(tolower(static_cast<unsigned char>(c)));
        char* end = nullptr;
        float num = strtof(s.value.c_str(), &end);
        bool isNum = !s.value.empty() && end && *end == 0;
        if (lv == "true" || lv == "false" || lv == "on" || lv == "off" || lv == "yes" || lv == "no") {
            e->isBool = true;
            e->onStr = (lv == "true" || lv == "false") ? "true" : (lv == "on" || lv == "off") ? "on" : "yes";
            e->offStr = e->onStr == "true" ? "false" : e->onStr == "on" ? "off" : "no";
            e->on = (lv == "true" || lv == "on" || lv == "yes");
            AddCheck(id, Pretty(s.key).c_str(), &e->on, SettingChanged, e.get());
        } else if (isNum) {
            e->value = num;
            float mn = num < 0 ? -1e6f : 0, mx = 1e9f;
            e->step = GuessStep(s.value, num);
            if (s.ranged) { mn = s.min; mx = s.max; if (s.step > 0) e->step = s.step; }
            AddNumber(id, Pretty(s.key).c_str(), &e->value, mn, mx, e->step, SettingChanged, e.get());
        } else {
            AddLabel(id, (Pretty(s.key) + ":  " + s.value).c_str());
            text = true;
            continue;
        }
        entries.push_back(std::move(e));
    }
    if (settings.empty()) AddLabel(id, i18n::Tr("No settings").c_str());
    if (text) AddLabel(id, i18n::Tr("(text settings: edit the mod's .ini in ArkMods)").c_str());
    {
        std::lock_guard<std::mutex> l(gMu);
        if (gWindows.count(id)) gWindows[id].listed = false;
        gSettingStore[id] = std::move(entries);
        gSettingsWin[owner] = id;
    }
    ShowWindow(id, 1);
}

void ApplyActions() {
    std::vector<Action> todo;
    todo.swap(gPending);
    for (const Action& a : todo) {
        switch (a.kind) {
            case Action::TogglePanel: SetOpen(!gOpen); break;
            case Action::ClosePanel: SetOpen(false); break;
            case Action::EnableMod:  // loading/unloading never happens in the middle of input handling
                dispatch::Post(ApplyEnable, new std::pair<std::string, bool>(a.file, a.enable), nullptr);
                break;
            case Action::FlipToggle: {
                bool alive;
                { std::lock_guard<std::mutex> l(gMu);
                  alive = std::any_of(gToggles.begin(), gToggles.end(),
                                      [&](const Toggle& t) { return t.value == a.value && t.owner == a.owner; }); }
                if (!alive) break;  // its mod was unloaded meanwhile
                *a.value = !*a.value;
                mods::SaveToggle(a.owner, a.label, *a.value);  // remembered across restarts
                break;
            }
            case Action::OpenWindow: ShowWindow(a.id, 1); break;
            case Action::OpenSettings: OpenSettingsWindow(a.owner, a.label); break;
            case Action::DumpSdk: {  // a few seconds on the game thread: the game pauses meanwhile
                sdk::Result r = sdk::Dump(mods::Dir() + "/sdk", "");
                if (gSdkRow.state) SetState(gSdkRow, i18n::Tr(r.ok ? "DONE" : "FAILED").c_str(), r.ok ? kOn : kOff);
                if (!r.ok) LOGE("sdk dump failed: %s", r.error.c_str());
                break;
            }
            case Action::CloseWindow: ShowWindow(a.id, 0); break;
            case Action::Click:
            case Action::Flip:
            case Action::Step: ApplyControl(a); break;
            case Action::Nothing: break;
        }
    }
}

int gFrame = 0;

void UiTick(void*) {
    ++gFrame;
    ApplyActions();
    const bool menu = engine::IsMainMenu();
    if (menu && gFrame % 10 == 0) AddMenuEntry();
    if (menu) for (auto& m : gMenuMirror) umg::Mirror(m.first, m.second);
    if (!gRoot) BuildOverlay();
    if (!gRoot) return;
    UObject* world = engine::World();
    if (world && world != gShownWorld && umg::ShowOnViewport(gRoot, 1000)) {  // map loads clear the viewport
        gShownWorld = world;
        SetOpen(false);
        CloseWindows();
        LOG("ui: MODS panel ready on the viewport (%s)", engine::Name(world).c_str());
    }
    bool closePanel;
    { std::lock_guard<std::mutex> l(gMu); closePanel = gClosePanel; gClosePanel = false; }
    if (closePanel && gOpen) SetOpen(false);
    UpdateWindows(gFrame % 10 == 0);
    if (gOpen && gFrame % 10 == 0) RefreshRows();
    // Hidden in the main menu (it has its own entry), under an open frame, and while the player has
    // no character (death / respawn screens: the button would cover the game's own UI there).
    static bool hasPawn = false;
    if (gFrame % 30 == 0) {
        static int pawnOff = engine::PropertyOffset(engine::FindObject("/Script/Engine.Controller"), "Pawn");
        UObject* pc = menu ? nullptr : engine::LocalPlayerController();
        hasPawn = pc && pawnOff >= 0 && At<UObject*>(pc, pawnOff);
    }
    const int showGameBtn = !menu && !AnyOpen() && hasPawn;
    if (showGameBtn != gGameBtnShown) {
        umg::SetVisibility(gGameBtn, showGameBtn ? umg::Vis::Visible : umg::Vis::Collapsed);
        gGameBtnShown = showGameBtn;
    }
}

// Every UButton click passes through here (Slate calls it on the game thread).
struct ReplyBlob {  // FReply, returned through x8 untouched
    alignas(16) uint8_t bytes[512];
    ~ReplyBlob() {}
};
ReplyBlob (*gOrigClicked)(UObject*) = nullptr;
ReplyBlob HookedClicked(UObject* button) {
    auto it = gActions.find(button);
    if (it != gActions.end()) gPending.push_back(it->second);
    return gOrigClicked(button);
}

// ------------------------------------------------------------------ HUD hook (mod drawings)
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
    gHud = nullptr;
}

// ------------------------------------------------------------------ touch (for mods' on_touch)
struct Vec2 { float x, y; };
bool (*gOrigTouchStarted)(void*, const void*, const Vec2&, int, int);
bool (*gOrigTouchMoved)(void*, const Vec2&, int, int);
bool (*gOrigTouchEnded)(void*, const Vec2&, int, int);

constexpr int kMaxFinger = 16;
enum class FingerOwner { None, Swallowed, Mod };
struct Finger {
    FingerOwner who = FingerOwner::None;
    Touch handler{};
};
Finger gFingers[kMaxFinger];

bool HookedTouchStarted(void* app, const void* win, const Vec2& p, int idx, int ctrl) {
    if (gW > 0 && !engine::IsMainMenu() && !AnyOpen()) {  // an open panel/window gets every touch
        Finger scratch;
        Finger& f = idx >= 0 && idx < kMaxFinger ? gFingers[idx] : scratch;
        f = Finger{};
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
        return true;
    }
    return gOrigTouchMoved(app, p, idx, ctrl);
}

bool HookedTouchEnded(void* app, const Vec2& p, int idx, int ctrl) {
    if (idx >= 0 && idx < kMaxFinger && gFingers[idx].who != FingerOwner::None) {
        Finger& f = gFingers[idx];
        if (f.who == FingerOwner::Mod) f.handler.fn(f.handler.user, 2, idx, p.x, p.y);
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
    if (!ok) return false;
    if (!umg::Init()) { LOGE("ui: UMG unavailable, no MODS panel"); return false; }
    if (!Hook(gOrigClicked, "_ZN7UButton18SlateHandleClickedEv", reinterpret_cast<void*>(&HookedClicked))) return false;
    gUmgReady = true;
    dispatch::AddTick(UiTick, nullptr, reinterpret_cast<const void*>(&Init));
    LOG("ui: MODS panel ready (UMG)");
    return true;
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
    mods::RestoreToggle(owner, label ? label : "?", value);  // last state the user chose
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
        if (f.who == FingerOwner::Mod && f.handler.owner == owner) f.who = FingerOwner::Swallowed;  // rest of the touch
    std::vector<int> drop;
    for (auto& w : gWindows) if (w.second.owner == owner) drop.push_back(w.first);
    for (int id : drop) DropWindowLocked(id);
}

// ------------------------------------------------------------------ mod windows (API)
int NewWindow(const char* title, const void* owner) {
    std::lock_guard<std::mutex> l(gMu);
    int id = gNextId++;
    Window& w = gWindows[id];
    w.title = title ? title : "";
    w.owner = owner;
    return id;
}

namespace {
int AddControl(int window, Control c) {
    std::lock_guard<std::mutex> l(gMu);
    auto w = gWindows.find(window);
    if (w == gWindows.end()) return 0;
    int id = gNextId++;
    c.window = window;
    gControls[id] = std::move(c);
    w->second.controls.push_back(id);
    ++w->second.version;
    return id;
}
}  // namespace

int AddLabel(int window, const char* text) {
    Control c;
    c.kind = Control::Label;
    c.text = text ? text : "";
    return AddControl(window, std::move(c));
}

int AddButton(int window, const char* text, ArkUiFn fn, void* user) {
    Control c;
    c.kind = Control::Button;
    c.text = text ? text : "";
    c.click = fn;
    c.user = user;
    return AddControl(window, std::move(c));
}

int AddCheck(int window, const char* label, int* value, ArkUiChangedFn fn, void* user) {
    if (!value) return 0;
    Control c;
    c.kind = Control::Check;
    c.text = label ? label : "";
    c.check = value;
    c.changed = fn;
    c.user = user;
    return AddControl(window, std::move(c));
}

int AddNumber(int window, const char* label, float* value, float min, float max, float step, ArkUiChangedFn fn, void* user) {
    if (!value || !(max >= min)) return 0;
    Control c;
    c.kind = Control::Number;
    c.text = label ? label : "";
    c.number = value;
    c.min = min;
    c.max = max;
    c.step = step > 0 ? step : 1;
    c.changed = fn;
    c.user = user;
    return AddControl(window, std::move(c));
}

void SetText(int id, const char* text) {
    std::lock_guard<std::mutex> l(gMu);
    if (auto c = gControls.find(id); c != gControls.end()) c->second.text = text ? text : "";
    else if (auto w = gWindows.find(id); w != gWindows.end()) w->second.title = text ? text : "";
}

void ShowWindow(int window, int mode) {
    std::lock_guard<std::mutex> l(gMu);
    auto w = gWindows.find(window);
    if (w == gWindows.end()) return;
    bool open = mode < 0 ? !w->second.open : mode != 0;
    if (open) {  // one frame at a time
        for (auto& o : gWindows) o.second.open = false;
        gClosePanel = true;
    }
    w->second.open = open;
}

bool WindowVisible(int window) {
    std::lock_guard<std::mutex> l(gMu);
    auto w = gWindows.find(window);
    return w != gWindows.end() && w->second.open;
}

}  // namespace ui
