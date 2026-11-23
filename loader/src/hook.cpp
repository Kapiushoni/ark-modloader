#include "hook.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

namespace hook {
namespace {

constexpr uint32_t kLdrX17_8 = 0x58000051;  // ldr x17, #8
constexpr uint32_t kBrX17    = 0xD61F0220;  // br x17

bool IsPcRelative(uint32_t i) {
    if ((i & 0x1F000000) == 0x10000000) return true;  // adr / adrp
    if ((i & 0x7C000000) == 0x14000000) return true;  // b / bl
    if ((i & 0xFF000010) == 0x54000000) return true;  // b.cond
    if ((i & 0x7E000000) == 0x34000000) return true;  // cbz / cbnz
    if ((i & 0x7E000000) == 0x36000000) return true;  // tbz / tbnz
    if ((i & 0x3B000000) == 0x18000000) return true;  // ldr (literal)
    return false;
}

void WriteJump(uint32_t* at, void* dest) {
    at[0] = kLdrX17_8;
    at[1] = kBrX17;
    std::memcpy(at + 2, &dest, sizeof(dest));
}

}  // namespace

void* InlineHook(void* target, void* replacement, const char** err) {
    auto* code = static_cast<uint32_t*>(target);
    for (int k = 0; k < 4; ++k) {
        if (IsPcRelative(code[k])) { *err = "pc-relative instruction in prologue"; return nullptr; }
    }

    // Trampoline: 4 original instructions + jump back to target+16.
    void* tramp = mmap(nullptr, 64, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED) { *err = "mmap trampoline failed"; return nullptr; }
    auto* t = static_cast<uint32_t*>(tramp);
    std::memcpy(t, code, 16);
    WriteJump(t + 4, reinterpret_cast<uint8_t*>(target) + 16);
    __builtin___clear_cache(reinterpret_cast<char*>(t), reinterpret_cast<char*>(t + 8));

    // Patch target.
    const uintptr_t page = sysconf(_SC_PAGESIZE);
    uintptr_t start = reinterpret_cast<uintptr_t>(target) & ~(page - 1);
    uintptr_t end   = (reinterpret_cast<uintptr_t>(target) + 16 + page - 1) & ~(page - 1);
    if (mprotect(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        *err = "mprotect target failed"; return nullptr;
    }
    WriteJump(code, replacement);
    __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code + 4));
    mprotect(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_EXEC);
    return tramp;
}

}  // namespace hook
