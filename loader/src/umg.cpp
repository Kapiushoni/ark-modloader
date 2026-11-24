#include "umg.h"

#include <cstring>
#include <map>
#include <string>

#include "dispatch.h"
#include "engine.h"
#include "log.h"

namespace umg {
namespace {

using ue4::At;

constexpr int32_t RF_Transient = 0x40;
constexpr int32_t kRootSet = 1 << 30;  // EInternalObjectFlags::RootSet (checked against GEngine in Init)

using StaticConstructFn = UObject* (*)(UObject* cls, UObject* outer, ue4::FName name, int32_t flags,
                                       int32_t internalFlags, UObject* tmpl, bool copyTransients,
                                       void* instancingGraph, bool assumeArchetype);
StaticConstructFn gConstruct = nullptr;
UObject* (*gTransientPackage)() = nullptr;

// TSharedRef<SWidget>: a user-provided destructor makes it non-trivial, so it is returned via x8
// and passed by reference exactly like the engine's type. We never release our reference: the
// Slate widget of a shown tree simply lives for the rest of the game.
struct SharedRef {
    void* object;
    void* controller;
    ~SharedRef() {}
};
SharedRef (*gTakeWidget)(UObject*) = nullptr;
void (*gAddToViewport)(UObject*, SharedRef, int32_t) = nullptr;
void (*gRemoveFromViewport)(UObject*, SharedRef) = nullptr;
UObject** gEngine = nullptr;

// ---------------------------------------------------------------- reflection calls
UObject* ClassOf(UObject* o) { return reinterpret_cast<UObject*>(o->ClassPrivate); }

UObject* FindFn(UObject* obj, const char* name) {
    static std::map<std::pair<UObject*, std::string>, UObject*> cache;
    UObject* cls = ClassOf(obj);
    auto key = std::make_pair(cls, std::string(name));
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    UObject* f = engine::FindField(cls, name);
    if (f && !engine::IsFunction(f)) f = nullptr;
    if (!f) LOGE("umg: %s has no function %s", engine::Name(cls).c_str(), name);
    return cache[key] = f;
}

bool IsParam(UObject* f, bool includeReturn) {
    uint64_t flags = At<uint64_t>(f, ue4::kProp_Flags);
    return (flags & ue4::CPF_Parm) && (includeReturn || !(flags & ue4::CPF_ReturnParm));
}

struct Call {
    UObject* fn = nullptr;
    alignas(16) uint8_t buf[512] = {};

    Call(UObject* obj, const char* name) {
        if (!obj) return;
        fn = FindFn(obj, name);
        if (fn && engine::ParamsSize(fn) > static_cast<int>(sizeof buf)) fn = nullptr;
    }
    // Writes the i-th parameter (return value excluded), copying at most its size.
    Call& Arg(int i, const void* data, size_t n) {
        if (!fn) return *this;
        for (UObject* f = At<UObject*>(fn, ue4::kStruct_Children); f; f = At<UObject*>(f, ue4::kField_Next)) {
            if (!IsParam(f, false) || i-- > 0) continue;
            size_t size = At<int32_t>(f, ue4::kProp_ElementSize);
            std::memcpy(buf + At<int32_t>(f, ue4::kProp_Offset), data, n < size ? n : size);
            break;
        }
        return *this;
    }
    template <typename T>
    Call& Arg(int i, const T& v) { return Arg(i, &v, sizeof v); }
    template <typename T>
    T Ret() {
        int o = fn ? engine::PropertyOffset(fn, "ReturnValue") : -1;
        return o >= 0 ? At<T>(buf, o) : T{};
    }
    bool Run(UObject* obj) {
        if (!fn || !obj) return false;
        dispatch::CallOriginal(obj, fn, buf);
        return true;
    }
};

// ---------------------------------------------------------------- struct layouts (by reflection)
// `strct` is a short name ("SlateColor"): looked up in SlateCore, then UMG.
int StructOffset(const char* strct, const char* field) {
    for (const char* module : {"/Script/SlateCore.", "/Script/UMG."}) {
        UObject* s = engine::FindObject((std::string(module) + strct).c_str());
        if (s) return engine::PropertyOffset(s, field);
    }
    LOGE("umg: no struct %s (for %s)", strct, field);
    return -1;
}

struct Layouts {
    int fontSize, colorSpecified, colorRule, childValue, childRule;
    int anchorOffsets, anchorAnchors, anchorAlignment;
} L;

// A copied widget must not share its template's place in the tree or its event bindings.
void Detach(UObject* obj) {
    for (UObject* s = ClassOf(obj); s; s = At<UObject*>(s, ue4::kStruct_Super)) {
        for (UObject* f = At<UObject*>(s, ue4::kStruct_Children); f; f = At<UObject*>(f, ue4::kField_Next)) {
            if (!engine::IsProperty(f)) continue;
            const std::string type = engine::TypeOf(f);
            const std::string name = engine::Name(f);
            auto* p = reinterpret_cast<uint8_t*>(obj) + At<int32_t>(f, ue4::kProp_Offset);
            size_t size = size_t(At<int32_t>(f, ue4::kProp_ElementSize)) * At<int32_t>(f, ue4::kProp_ArrayDim);
            if (type == "MulticastDelegateProperty" || type == "DelegateProperty") std::memset(p, 0, size);
            else if (name == "Slot" && type == "ObjectProperty") std::memset(p, 0, size);
            else if (name == "Slots" && type == "ArrayProperty") std::memset(p, 0, size);  // copied array leaks, tiny
        }
    }
}

}  // namespace

bool Init() {
    gConstruct = reinterpret_cast<StaticConstructFn>(engine::Sym(
        "_Z30StaticConstructObject_InternalP6UClassP7UObject5FName12EObjectFlags20EInternalObjectFlagsS2_bP22FObjectInstancingGraphb"));
    gTransientPackage = reinterpret_cast<UObject* (*)()>(engine::Sym("_Z19GetTransientPackagev"));
    gTakeWidget = reinterpret_cast<SharedRef (*)(UObject*)>(engine::Sym("_ZN7UWidget10TakeWidgetEv"));
    gEngine = reinterpret_cast<UObject**>(engine::Sym("GEngine"));
    if (!gConstruct || !gTransientPackage || !gTakeWidget || !gEngine) { LOGE("umg: engine functions missing"); return false; }

    L.fontSize = StructOffset("SlateFontInfo", "Size");
    L.colorSpecified = StructOffset("SlateColor", "SpecifiedColor");
    L.colorRule = StructOffset("SlateColor", "ColorUseRule");
    L.childValue = StructOffset("SlateChildSize", "Value");
    L.childRule = StructOffset("SlateChildSize", "SizeRule");
    L.anchorOffsets = StructOffset("AnchorData", "Offsets");
    L.anchorAnchors = StructOffset("AnchorData", "Anchors");
    L.anchorAlignment = StructOffset("AnchorData", "Alignment");
    if (L.fontSize < 0 || L.colorSpecified < 0 || L.colorRule < 0 || L.childValue < 0 || L.childRule < 0 ||
        L.anchorOffsets < 0 || L.anchorAnchors < 0 || L.anchorAlignment < 0)
        return false;

    // The engine object is rooted: confirm our RootSet bit before relying on it.
    UObject* eng = *gEngine;
    int32_t flags = eng ? engine::Objects()->ObjObjects.Objects[eng->InternalIndex].Flags : 0;
    if (!(flags & kRootSet)) { LOGE("umg: RootSet flag mismatch (GEngine flags %08x)", flags); return false; }
    return true;
}

UObject* Class(const char* name) {
    static std::map<std::string, UObject*> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    UObject* c = engine::FindObject(("/Script/UMG." + std::string(name)).c_str());
    if (!c) LOGE("umg: class %s not found", name);
    return cache[name] = c;
}

UObject* New(UObject* cls, UObject* tmpl, UObject* outer, bool root) {
    if (!cls || !gConstruct) return nullptr;
    if (tmpl && !engine::IsA(tmpl, cls)) tmpl = nullptr;
    UObject* o = gConstruct(cls, outer ? outer : gTransientPackage(), ue4::FName{0, 0}, RF_Transient,
                            root ? kRootSet : 0, tmpl, false, nullptr, false);
    if (o && tmpl) Detach(o);
    return o;
}

UObject* AddChild(UObject* panel, UObject* child) {
    Call c(panel, "AddChild");
    c.Arg(0, child).Run(panel);
    return c.Ret<UObject*>();
}

void ClearChildren(UObject* panel) { Call(panel, "ClearChildren").Run(panel); }

int ChildCount(UObject* panel) {
    Call c(panel, "GetChildrenCount");
    return c.Run(panel) ? c.Ret<int32_t>() : 0;
}

UObject* ChildAt(UObject* panel, int i) {
    Call c(panel, "GetChildAt");
    return c.Arg(0, int32_t(i)).Run(panel) ? c.Ret<UObject*>() : nullptr;
}

UObject* SlotOf(UObject* w) {
    int o = w ? engine::PropertyOffset(w, "Slot") : -1;
    return o >= 0 ? At<UObject*>(w, o) : nullptr;
}

UObject* ParentOf(UObject* w) {
    UObject* s = SlotOf(w);
    int o = s ? engine::PropertyOffset(s, "Parent") : -1;
    return o >= 0 ? At<UObject*>(s, o) : nullptr;
}

void SetVisibility(UObject* w, Vis v) { Call(w, "SetVisibility").Arg(0, v).Run(w); }

void SetText(UObject* tb, const char* utf8) {
    ue4::FText t = engine::MakeText(utf8);  // one FText reference leaks per call (as everywhere)
    Call(tb, "SetText").Arg(0, t).Run(tb);
}

void SetTextColor(UObject* tb, float r, float g, float b, float a) {
    uint8_t color[64] = {};
    float rgba[4] = {r, g, b, a};
    std::memcpy(color + L.colorSpecified, rgba, sizeof rgba);
    color[L.colorRule] = 0;  // ESlateColorStylingMode::UseColor_Specified
    Call(tb, "SetColorAndOpacity").Arg(0, color, sizeof color).Run(tb);
}

void SetFontSize(UObject* tb, int size) {
    int font = tb ? engine::PropertyOffset(tb, "Font") : -1;
    if (font >= 0) At<int32_t>(tb, font + L.fontSize) = size;
}

void SetButtonColor(UObject* btn, float r, float g, float b, float a) {
    float rgba[4] = {r, g, b, a};
    Call(btn, "SetBackgroundColor").Arg(0, rgba).Run(btn);
}

void SetPreciseTap(UObject* btn) {
    int o = btn ? engine::PropertyOffset(btn, "TouchMethod") : -1;
    if (o >= 0) At<uint8_t>(btn, o) = 1;  // EButtonTouchMethod::PreciseTap (4.17: DownAndUp, PreciseTap)
}

void SetBorderColor(UObject* border, float r, float g, float b, float a) {
    float rgba[4] = {r, g, b, a};
    Call(border, "SetBrushColor").Arg(0, rgba).Run(border);
}

void SetBorderPadding(UObject* border, float l, float t, float r, float b) {
    float m[4] = {l, t, r, b};
    Call(border, "SetPadding").Arg(0, m).Run(border);
}

void SetPadding(UObject* slot, float l, float t, float r, float b) {
    float m[4] = {l, t, r, b};
    Call(slot, "SetPadding").Arg(0, m).Run(slot);
}

void SetFill(UObject* slot, float weight) {
    uint8_t size[16] = {};
    std::memcpy(size + L.childValue, &weight, sizeof weight);
    size[L.childRule] = 1;  // ESlateSizeRule::Fill
    Call(slot, "SetSize").Arg(0, size, sizeof size).Run(slot);
}

void SetAlign(UObject* slot, HAlign h, VAlign v) {
    Call(slot, "SetHorizontalAlignment").Arg(0, h).Run(slot);
    Call(slot, "SetVerticalAlignment").Arg(0, v).Run(slot);
}

void SetCanvasLayout(UObject* slot, float minX, float minY, float maxX, float maxY,
                     float left, float top, float right, float bottom, float alignX, float alignY) {
    uint8_t data[64] = {};
    float offsets[4] = {left, top, right, bottom}, anchors[4] = {minX, minY, maxX, maxY}, align[2] = {alignX, alignY};
    std::memcpy(data + L.anchorOffsets, offsets, sizeof offsets);
    std::memcpy(data + L.anchorAnchors, anchors, sizeof anchors);
    std::memcpy(data + L.anchorAlignment, align, sizeof align);
    Call(slot, "SetLayout").Arg(0, data, sizeof data).Run(slot);
}

void CopySlot(UObject* dst, UObject* src) {
    if (!dst || !src || ClassOf(dst) != ClassOf(src)) return;
    for (UObject* s = ClassOf(dst); s; s = At<UObject*>(s, ue4::kStruct_Super)) {
        for (UObject* f = At<UObject*>(s, ue4::kStruct_Children); f; f = At<UObject*>(f, ue4::kField_Next)) {
            if (!engine::IsFunction(f)) continue;
            std::string fn = engine::Name(f);
            if (fn.size() < 4 || fn.compare(0, 3, "Set") != 0) continue;
            std::string field = fn == "SetLayout" ? "LayoutData" : fn.substr(3);
            UObject* prop = engine::FindField(src, field.c_str());
            if (!prop || !engine::IsProperty(prop)) continue;
            const std::string type = engine::TypeOf(prop);
            if (type == "ObjectProperty" || type == "BoolProperty") continue;  // tree links / bitfields
            int count = 0;
            for (UObject* p = At<UObject*>(f, ue4::kStruct_Children); p; p = At<UObject*>(p, ue4::kField_Next))
                count += IsParam(p, true);
            if (count != 1) continue;
            Call c(dst, fn.c_str());
            c.Arg(0, reinterpret_cast<uint8_t*>(src) + At<int32_t>(prop, ue4::kProp_Offset),
                  At<int32_t>(prop, ue4::kProp_ElementSize));
            c.Run(dst);
        }
    }
}

void Mirror(UObject* dst, UObject* src) {
    static const struct { const char* prop; const char* setter; } kFields[] = {
        {"RenderTransform", "SetRenderTransform"}, {"RenderOpacity", "SetRenderOpacity"}, {"Visibility", "SetVisibility"}};
    if (!engine::IsValid(dst) || !engine::IsValid(src)) return;
    for (const auto& f : kFields) {
        UObject* p = engine::FindField(src, f.prop);
        if (!p || !engine::IsProperty(p)) continue;
        int off = At<int32_t>(p, ue4::kProp_Offset), size = At<int32_t>(p, ue4::kProp_ElementSize);
        const uint8_t* from = reinterpret_cast<uint8_t*>(src) + off;
        if (std::memcmp(from, reinterpret_cast<uint8_t*>(dst) + off, size) != 0) Call(dst, f.setter).Arg(0, from, size).Run(dst);
    }
}

bool Call1(UObject* obj, const char* fn, const void* arg, size_t size) {
    Call c(obj, fn);
    return c.Arg(0, arg, size).Run(obj);
}

void ScaleCanvasHeight(UObject* slot, float factor) {
    int o = slot ? engine::PropertyOffset(slot, "LayoutData") : -1;
    if (o < 0) return;
    uint8_t data[64] = {};
    std::memcpy(data, reinterpret_cast<uint8_t*>(slot) + o, 40);  // FAnchorData
    float* off = reinterpret_cast<float*>(data + L.anchorOffsets);
    float* anc = reinterpret_cast<float*>(data + L.anchorAnchors);
    if (anc[1] != anc[3]) anc[3] = anc[1] + (anc[3] - anc[1]) * factor;  // stretched: grow the anchor span
    else off[3] *= factor;                                                // point anchor: Offsets.Bottom = height
    Call(slot, "SetLayout").Arg(0, data, sizeof data).Run(slot);
}

bool ShowOnViewport(UObject* widget, int zOrder) {
    UObject* eng = *gEngine;
    int o = eng ? engine::PropertyOffset(eng, "GameViewport") : -1;
    UObject* vp = o >= 0 ? At<UObject*>(eng, o) : nullptr;
    if (!vp || !widget) return false;
    if (!gAddToViewport) {
        // ARK's viewport client overrides these (non-virtual call: pick the most derived one).
        bool shooter = engine::IsA(vp, engine::FindObject("/Script/ShooterGame.ShooterGameViewportClient"));
        const char* cls = shooter ? "26UShooterGameViewportClient" : "19UGameViewportClient";
        std::string add = std::string("_ZN") + cls + "24AddViewportWidgetContentE10TSharedRefI7SWidgetL7ESPMode0EEi";
        std::string rem = std::string("_ZN") + cls + "27RemoveViewportWidgetContentE10TSharedRefI7SWidgetL7ESPMode0EE";
        gAddToViewport = reinterpret_cast<decltype(gAddToViewport)>(engine::Sym(add.c_str()));
        gRemoveFromViewport = reinterpret_cast<decltype(gRemoveFromViewport)>(engine::Sym(rem.c_str()));
        if (!gAddToViewport || !gRemoveFromViewport) { LOGE("umg: viewport functions missing"); return false; }
    }
    SharedRef w = gTakeWidget(widget);
    gRemoveFromViewport(vp, w);  // no-op unless still there: never add the same widget twice
    gAddToViewport(vp, w, zOrder);
    return true;
}

void HideFromViewport(UObject* widget) {
    UObject* eng = *gEngine;
    int o = eng ? engine::PropertyOffset(eng, "GameViewport") : -1;
    UObject* vp = o >= 0 ? At<UObject*>(eng, o) : nullptr;
    if (vp && widget && gRemoveFromViewport) gRemoveFromViewport(vp, gTakeWidget(widget));
}

}  // namespace umg
