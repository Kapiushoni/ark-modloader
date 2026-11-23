// ProcessEvent hook: per-UFunction pre/post callbacks, game-thread tasks, per-frame ticks.
// Every registration remembers its owner (base address of the module that registered it)
// so a mod's registrations can be dropped when it is unloaded.
#pragma once
#include "arkmod.h"
#include "ue4.h"

namespace dispatch {

bool Install(void* processEvent);
void CallOriginal(ue4::UObject* obj, ue4::UObject* func, void* params);

int  AddHook(const char* pattern, ArkPreHook pre, ArkPostHook post, void* user, const void* owner);
void Post(ArkTask task, void* user, const void* owner);
void AddTick(ArkTask task, void* user, const void* owner);
void RemoveOwner(const void* owner);

bool OnGameThread();
uint64_t CallCount();

}  // namespace dispatch
