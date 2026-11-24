// Engine events for mods: "an object of class X was created".
// The engine reports creations through FUObjectArray's create listeners (any thread, object not
// constructed yet); we queue matching ones and deliver them on the game thread next tick.
#pragma once
#include "ue4.h"

namespace events {

using NewObjectFn = void (*)(void* user, ue4::UObject* obj);

bool Init();  // once the engine is ready
void OnNewObject(ue4::UObject* cls, NewObjectFn fn, void* user, const void* owner);
void RemoveOwner(const void* owner);

}  // namespace events
