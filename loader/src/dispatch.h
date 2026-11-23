// ProcessEvent hook: per-UFunction pre/post callbacks, game-thread tasks, per-frame ticks.
#pragma once
#include "arkmod.h"
#include "ue4.h"

namespace dispatch {

bool Install(void* processEvent);
void CallOriginal(ue4::UObject* obj, ue4::UObject* func, void* params);

int  AddHook(const char* pattern, ArkPreHook pre, ArkPostHook post, void* user);
void Post(ArkTask task, void* user);
void AddTick(ArkTask task, void* user);

uint64_t CallCount();

}  // namespace dispatch
