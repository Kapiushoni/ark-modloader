#include "engine.h"

#include <dlfcn.h>

#include <cstring>

#include "log.h"

namespace engine {
using namespace ue4;
namespace {

void* gLib = nullptr;
FUObjectArray* gObjects = nullptr;
TNameEntryArray* (*gGetNames)() = nullptr;
UObject** gWorld = nullptr;
uint32_t* gGameThreadId = nullptr;
uint64_t* gFrameCounter = nullptr;
UObject* (*gStaticFindObject)(UObject*, UObject*, const TCHAR*, bool) = nullptr;
FText (*gTextFromString)(const FString&) = nullptr;
const FString& (*gTextToString)(const FText*) = nullptr;
void (*gMemFree)(void*) = nullptr;

// Non-trivial so the AArch64 ABI returns it indirectly (x8), like the real FString.
struct FStringRet {
    FString s;
    ~FStringRet() {}
};
FStringRet (*gConsoleCommand)(UObject*, const FString&, bool) = nullptr;

TNameEntryArray* Names() { return gGetNames ? gGetNames() : nullptr; }

}  // namespace

void* Sym(const char* name) { return gLib ? dlsym(gLib, name) : nullptr; }

template <typename T>
static bool Resolve(T& out, const char* name) {
    out = reinterpret_cast<T>(Sym(name));
    if (!out) LOGE("missing symbol %s", name);
    return out != nullptr;
}

bool Init(void* lib) {
    gLib = lib;
    bool ok = true;
    ok &= Resolve(gObjects, "GUObjectArray");
    ok &= Resolve(gGetNames, "_ZN5FName8GetNamesEv");
    ok &= Resolve(gWorld, "GWorld");
    ok &= Resolve(gGameThreadId, "GGameThreadId");
    ok &= Resolve(gFrameCounter, "GFrameCounter");
    ok &= Resolve(gStaticFindObject, "_Z16StaticFindObjectP6UClassP7UObjectPKwb");
    ok &= Resolve(gTextFromString, "_ZN5FText10FromStringERK7FString");
    ok &= Resolve(gTextToString, "_ZNK5FText8ToStringEv");
    ok &= Resolve(gMemFree, "_ZN7FMemory4FreeEPv");
    ok &= Resolve(gConsoleCommand, "_ZN17APlayerController14ConsoleCommandERK7FStringb");
    return ok;
}

bool Ready() {
    TNameEntryArray* n = Names();
    return gObjects && gObjects->ObjObjects.NumElements > 1000 && n && n->NumElements > 1000;
}

FUObjectArray* Objects() { return gObjects; }
UObject* World() { return gWorld ? *gWorld : nullptr; }
uint32_t GameThreadId() { return gGameThreadId ? *gGameThreadId : 0; }
uint64_t FrameCounter() { return gFrameCounter ? *gFrameCounter : 0; }

// ---------------------------------------------------------------- strings

std::string ToUtf8(const TCHAR* s) {
    std::string out;
    for (; s && *s; ++s) {
        uint32_t c = static_cast<uint32_t>(*s);
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
        else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::wstring FromUtf8(const char* s) {
    std::wstring out;
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    while (p && *p) {
        uint32_t c = *p++;
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra) c &= (0x3F >> extra);
        for (; extra > 0 && (*p & 0xC0) == 0x80; --extra) c = (c << 6) | (*p++ & 0x3F);
        out += wchar_t(c);
    }
    return out;
}

static FString TempFString(std::wstring& w) {
    FString f;
    f.Data = w.data();
    f.Num = f.Max = static_cast<int32_t>(w.size() + 1);
    return f;
}

// ---------------------------------------------------------------- names / objects

std::string Name(UObject* o) {
    if (!o) return "None";
    TNameEntryArray* names = Names();
    FNameEntry* e = names ? names->Get(o->NamePrivate.ComparisonIndex) : nullptr;
    if (!e) return "<bad name>";
    std::string s = e->IsWide() ? ToUtf8(e->WideName) : std::string(e->AnsiName);
    if (o->NamePrivate.Number > 0) s += "_" + std::to_string(o->NamePrivate.Number - 1);
    return s;
}

std::string Path(UObject* o) {
    if (!o) return "None";
    std::string p = Name(o);
    for (UObject* outer = o->OuterPrivate; outer; outer = outer->OuterPrivate) p = Name(outer) + "." + p;
    return p;
}

std::string ClassAndPath(UObject* o) {
    return o ? Name(reinterpret_cast<UObject*>(o->ClassPrivate)) + " " + Path(o) : "None";
}

UObject* FindObject(const char* path) {
    std::wstring w = FromUtf8(path);
    return gStaticFindObject(nullptr, reinterpret_cast<UObject*>(-1) /*ANY_PACKAGE*/, w.c_str(), false);
}

bool IsA(UObject* o, UObject* cls) {
    if (!o || !cls) return false;
    for (UObject* c = reinterpret_cast<UObject*>(o->ClassPrivate); c; c = At<UObject*>(c, kStruct_Super))
        if (c == cls) return true;
    return false;
}

bool IsCDO(UObject* o) { return o && (o->ObjectFlags & RF_ClassDefaultObject); }

// ---------------------------------------------------------------- reflection

static UObject* StructClass() {
    static UObject* c = FindObject("/Script/CoreUObject.Struct");
    return c;
}
static UObject* PropertyClass() {
    static UObject* c = FindObject("/Script/CoreUObject.Property");
    return c;
}

UObject* FindField(UObject* strct, const char* name) {
    if (strct && !IsA(strct, StructClass()))
        strct = reinterpret_cast<UObject*>(strct->ClassPrivate);  // instance -> its class
    for (UObject* s = strct; s; s = At<UObject*>(s, kStruct_Super))
        for (UObject* f = At<UObject*>(s, kStruct_Children); f; f = At<UObject*>(f, kField_Next))
            if (Name(f) == name) return f;
    return nullptr;
}

int PropertyOffset(UObject* strct, const char* name) {
    UObject* p = FindField(strct, name);
    if (!p || !IsA(p, PropertyClass())) return -1;
    return At<int32_t>(p, kProp_Offset);
}

int ParamsSize(UObject* func) {
    int size = 0;
    for (UObject* f = At<UObject*>(func, kStruct_Children); f; f = At<UObject*>(f, kField_Next)) {
        if (!(At<uint64_t>(f, kProp_Flags) & CPF_Parm)) continue;
        int end = At<int32_t>(f, kProp_Offset) + At<int32_t>(f, kProp_ElementSize) * At<int32_t>(f, kProp_ArrayDim);
        if (end > size) size = end;
    }
    return std::max(size, At<int32_t>(func, kStruct_PropsSize));
}

// ---------------------------------------------------------------- text / commands

FText MakeText(const char* utf8) {
    std::wstring w = FromUtf8(utf8);
    FString f = TempFString(w);
    return gTextFromString(f);  // FromString copies the string
}

std::string TextToUtf8(const FText& t) {
    const FString& s = gTextToString(&t);
    return s.Data ? ToUtf8(s.Data) : std::string();
}

bool ExecCommand(const char* cmd) {
    UObject* pcClass = FindObject("/Script/Engine.PlayerController");
    int playerOff = PropertyOffset(pcClass, "Player");
    if (!pcClass || playerOff < 0) return false;
    FFixedUObjectArray& arr = gObjects->ObjObjects;
    for (int i = 0; i < arr.NumElements; ++i) {
        UObject* o = arr.Objects[i].Object;
        if (!o || IsCDO(o) || !IsA(o, pcClass) || !At<void*>(o, playerOff)) continue;
        std::wstring w = FromUtf8(cmd);
        FString f = TempFString(w);
        FStringRet r = gConsoleCommand(o, f, true);
        if (r.s.Data) gMemFree(r.s.Data);
        return true;
    }
    return false;
}

}  // namespace engine
