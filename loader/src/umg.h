// Minimal runtime UMG toolkit: create widgets (optionally copying the look of a live game widget),
// call their BlueprintCallable setters through reflection, and put a widget tree on the viewport.
// Everything here must run on the game thread.
#pragma once
#include <cstdint>

#include "ue4.h"

namespace umg {

using ue4::UObject;

enum class Vis : uint8_t { Visible = 0, Collapsed = 1, Hidden = 2, HitTestInvisible = 3, SelfHitTestInvisible = 4 };
enum HAlign : uint8_t { HFill = 0, HLeft = 1, HCenter = 2, HRight = 3 };
enum VAlign : uint8_t { VFill = 0, VTop = 1, VCenter = 2, VBottom = 3 };

bool Init();

UObject* Class(const char* name);  // "Button" -> /Script/UMG.Button (cached)

// New widget of `cls`. With `tmpl` its properties (style, font, ...) are copied from that widget,
// minus its parent slot, children and bound events. `outer` defaults to the transient package.
// `root` keeps it from garbage collection (use for the top of a tree nobody else references).
UObject* New(UObject* cls, UObject* tmpl = nullptr, UObject* outer = nullptr, bool root = false);

// Panels
UObject* AddChild(UObject* panel, UObject* child);  // returns the new slot
void ClearChildren(UObject* panel);
int ChildCount(UObject* panel);
UObject* ChildAt(UObject* panel, int i);
UObject* SlotOf(UObject* widget);
UObject* ParentOf(UObject* widget);

// Widgets
void SetVisibility(UObject* w, Vis v);
void SetText(UObject* textBlock, const char* utf8);
void SetTextColor(UObject* textBlock, float r, float g, float b, float a = 1);
void SetFontSize(UObject* textBlock, int size);  // before the widget is first shown
void SetButtonColor(UObject* button, float r, float g, float b, float a);
void SetPreciseTap(UObject* button);             // before first shown: taps only, drags scroll
void SetBorderColor(UObject* border, float r, float g, float b, float a);
void SetBorderPadding(UObject* border, float l, float t, float r, float b);

// Slots
void SetPadding(UObject* slot, float l, float t, float r, float b);
void SetFill(UObject* slot, float weight);  // box slot: take a share of the free space
void SetAlign(UObject* slot, HAlign h, VAlign v);
void SetCanvasLayout(UObject* slot, float minX, float minY, float maxX, float maxY,
                     float left, float top, float right, float bottom, float alignX, float alignY);
void CopySlot(UObject* dst, UObject* src);  // same slot class: re-applies every SetXxx(Xxx)
void ScaleCanvasHeight(UObject* canvasSlot, float factor);

// Makes `dst` follow `src`'s render transform, opacity and visibility (what menu animations
// change). Cheap when nothing changed; call every frame.
void Mirror(UObject* dst, UObject* src);

// Any one-parameter UFunction (e.g. a setter not wrapped above).
bool Call1(UObject* obj, const char* fn, const void* arg, size_t size);
template <typename T>
bool Call1(UObject* obj, const char* fn, const T& arg) { return Call1(obj, fn, &arg, sizeof arg); }

// Viewport: add (again) / remove a widget tree drawn above the game and its menus.
bool ShowOnViewport(UObject* widget, int zOrder);
void HideFromViewport(UObject* widget);

}  // namespace umg
