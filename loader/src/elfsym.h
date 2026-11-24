// Symbol lookup in an already-loaded ELF without dlopen/dlsym.
// Needed when the loader lives in a different linker namespace than libUE4 (Zygisk injection):
// there dlopen("libUE4.so", RTLD_NOLOAD) can't see the game's library, but dl_iterate_phdr can.
#pragma once

#include <link.h>

#include <cstdint>

namespace elfsym {

struct Image {
    uintptr_t bias = 0;
    const ElfW(Sym)* symtab = nullptr;
    const char* strtab = nullptr;
    const uint32_t* gnuHash = nullptr;
    const uint32_t* sysvHash = nullptr;
    bool Valid() const { return symtab && strtab && (gnuHash || sysvHash); }
};

// Finds a loaded library whose path ends with `suffix` (e.g. "/libUE4.so").
bool Find(const char* suffix, Image* out);
// Address of a defined dynamic symbol, or nullptr.
void* Lookup(const Image& img, const char* name);

}  // namespace elfsym
