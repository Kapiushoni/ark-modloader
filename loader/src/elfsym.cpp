#include "elfsym.h"

#include <sys/auxv.h>

#include <cstring>

namespace elfsym {
namespace {

struct Search {
    const char* suffix;
    size_t suffixLen;
    Image* out;
    bool found;
};

uintptr_t Ptr(uintptr_t bias, ElfW(Addr) v) {
    // Bionic keeps d_ptr unrelocated; other loaders may have rebased it already.
    return v < bias ? bias + v : v;
}

int OnPhdr(dl_phdr_info* info, size_t, void* data) {
    auto* s = static_cast<Search*>(data);
    if (!info->dlpi_name || !info->dlpi_phdr || info->dlpi_phnum == 0) return 0;
    size_t n = strlen(info->dlpi_name);
    if (n < s->suffixLen || strcmp(info->dlpi_name + n - s->suffixLen, s->suffix) != 0) return 0;

    const ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i)
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            dyn = reinterpret_cast<const ElfW(Dyn)*>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    if (!dyn) return 0;

    Image img;
    img.bias = info->dlpi_addr;
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        uintptr_t p = Ptr(img.bias, dyn->d_un.d_ptr);
        switch (dyn->d_tag) {
            case DT_SYMTAB: img.symtab = reinterpret_cast<const ElfW(Sym)*>(p); break;
            case DT_STRTAB: img.strtab = reinterpret_cast<const char*>(p); break;
            case DT_GNU_HASH: img.gnuHash = reinterpret_cast<const uint32_t*>(p); break;
            case DT_HASH: img.sysvHash = reinterpret_cast<const uint32_t*>(p); break;
        }
    }
    if (!img.Valid()) return 0;
    *s->out = img;
    s->found = true;
    return 1;
}

uint32_t GnuHash(const char* s) {
    uint32_t h = 5381;
    for (; *s; ++s) h = h * 33 + static_cast<uint8_t>(*s);
    return h;
}

uint32_t SysvHash(const char* s) {
    uint32_t h = 0;
    for (; *s; ++s) {
        h = (h << 4) + static_cast<uint8_t>(*s);
        uint32_t g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

void* Defined(const Image& img, uint32_t idx, const char* name) {
    const ElfW(Sym)& s = img.symtab[idx];
    if (s.st_shndx == SHN_UNDEF || s.st_value == 0) return nullptr;
    if (strcmp(img.strtab + s.st_name, name) != 0) return nullptr;
    uintptr_t addr = img.bias + s.st_value;
    if (ELF_ST_TYPE(s.st_info) == STT_GNU_IFUNC) {  // value is a resolver, as dlsym would call it
#if defined(__aarch64__)
        // Same arguments as bionic's linker: (hwcap | _IFUNC_ARG_HWCAP, &__ifunc_arg_t).
        struct IfuncArg { unsigned long size, hwcap, hwcap2; };
        static IfuncArg arg{sizeof(IfuncArg), getauxval(AT_HWCAP), getauxval(AT_HWCAP2)};
        addr = reinterpret_cast<uintptr_t (*)(uint64_t, IfuncArg*)>(addr)(arg.hwcap | (1ULL << 62), &arg);
#else
        addr = reinterpret_cast<uintptr_t (*)()>(addr)();
#endif
    }
    return reinterpret_cast<void*>(addr);
}

}  // namespace

bool Find(const char* suffix, Image* out) {
    Search s{suffix, strlen(suffix), out, false};
    dl_iterate_phdr(OnPhdr, &s);
    return s.found;
}

void* Lookup(const Image& img, const char* name) {
    if (!img.Valid()) return nullptr;
    if (const uint32_t* h = img.gnuHash) {
        uint32_t nbuckets = h[0], symoffset = h[1], bloomSize = h[2];
        auto* bloom = reinterpret_cast<const ElfW(Addr)*>(h + 4);
        auto* buckets = reinterpret_cast<const uint32_t*>(bloom + bloomSize);
        const uint32_t* chain = buckets + nbuckets;
        uint32_t hash = GnuHash(name);
        uint32_t idx = nbuckets ? buckets[hash % nbuckets] : 0;
        if (idx < symoffset) return nullptr;
        for (;; ++idx) {
            uint32_t ch = chain[idx - symoffset];
            if ((ch | 1) == (hash | 1))
                if (void* p = Defined(img, idx, name)) return p;
            if (ch & 1) return nullptr;
        }
    }
    const uint32_t* h = img.sysvHash;
    uint32_t nbucket = h[0];
    const uint32_t* bucket = h + 2;
    const uint32_t* chain = bucket + nbucket;
    for (uint32_t idx = bucket[SysvHash(name) % nbucket]; idx != 0; idx = chain[idx])
        if (void* p = Defined(img, idx, name)) return p;
    return nullptr;
}

}  // namespace elfsym
