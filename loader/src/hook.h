// Tiny arm64 inline hook: patches the first 16 bytes of a function with
//   ldr x17, #8 ; br x17 ; .quad replacement
// and builds a trampoline from the original 4 instructions. Refuses targets
// whose first 4 instructions are PC-relative (those would need relocation).
#pragma once
#include <cstdint>

namespace hook {

// Returns trampoline (call it to reach the original) or nullptr on failure.
// `err` receives a short reason on failure.
void* InlineHook(void* target, void* replacement, const char** err);

}  // namespace hook
