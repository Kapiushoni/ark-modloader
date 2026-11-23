// Minimal UE 4.17 (Android arm64, ARK Mobile 2.0.29) engine layouts.
// Offsets verified against libUE4.so disassembly (see re/ notes).
#pragma once
#include <cstdint>
#include <string>

namespace ue4 {

using TCHAR = wchar_t;  // mangled symbols use wchar_t (4 bytes on Android)

struct FName {
    int32_t ComparisonIndex;
    int32_t Number;
};

struct UObject;
struct UClass;

// UObjectBase
struct UObject {
    void**   VTable;          // 0x00
    int32_t  ObjectFlags;     // 0x08
    int32_t  InternalIndex;   // 0x0C  (ProcessEvent: ldrsw x9,[x0,#0xc])
    UClass*  ClassPrivate;    // 0x10
    FName    NamePrivate;     // 0x18
    UObject* OuterPrivate;    // 0x20
};
static_assert(sizeof(UObject) == 0x28, "UObjectBase size");

struct FUObjectItem {
    UObject* Object;
    int32_t  Flags;
    int32_t  ClusterRootIndex;
    int32_t  SerialNumber;
};
static_assert(sizeof(FUObjectItem) == 0x18, "FUObjectItem size (madd *0x18)");

struct FFixedUObjectArray {
    FUObjectItem* Objects;
    int32_t MaxElements;
    int32_t NumElements;
};

struct FUObjectArray {
    int32_t ObjFirstGCIndex;
    int32_t ObjLastNonGCIndex;
    int32_t MaxObjectsNotConsideredByGC;
    bool    OpenForDisregardForGC;
    FFixedUObjectArray ObjObjects;  // 0x10  (ProcessEvent: ldr x8,[x8,#0x10])
};

// FNameEntry (4.17): Index low bit = wide flag.
struct FNameEntry {
    int32_t     Index;
    FNameEntry* HashNext;
    union {
        char    AnsiName[1024];
        TCHAR   WideName[1024];
    };
    bool IsWide() const { return Index & 1; }
};

// TStaticIndirectArrayThreadSafeRead<FNameEntry, 2*1024*1024, 16384>  (size 0x408)
struct TNameEntryArray {
    static constexpr int ElementsPerChunk = 16384;
    FNameEntry** Chunks[128];
    int32_t NumElements;
    int32_t NumChunks;

    FNameEntry* Get(int32_t i) const {
        if (i < 0 || i >= NumElements) return nullptr;
        FNameEntry** c = Chunks[i / ElementsPerChunk];
        return c ? c[i % ElementsPerChunk] : nullptr;
    }
};

// --- reflection (offsets verified at runtime on UMG.TextBlock) ---
constexpr int kField_Next          = 0x28;  // UField::Next
constexpr int kStruct_Super        = 0x30;  // UStruct::SuperStruct
constexpr int kStruct_Children     = 0x38;  // UStruct::Children
constexpr int kStruct_PropsSize    = 0x40;  // UStruct::PropertiesSize
constexpr int kProp_ArrayDim       = 0x30;
constexpr int kProp_ElementSize    = 0x34;
constexpr int kProp_Flags          = 0x38;  // uint64 EPropertyFlags
constexpr int kProp_Offset         = 0x50;  // Offset_Internal
constexpr int kProp_Extra         = 0x78;  // Struct* / PropertyClass* / Inner* / Enum* (verified)
constexpr int kBool_ByteOffset     = 0x79;  // UBoolProperty::ByteOffset
constexpr int kBool_ByteMask       = 0x7A;  // UBoolProperty::ByteMask
constexpr uint64_t CPF_Parm        = 0x80;
constexpr uint64_t CPF_OutParm     = 0x100;
constexpr uint64_t CPF_ReturnParm  = 0x400;
constexpr int32_t  RF_ClassDefaultObject = 0x10;

template <typename T>
inline T& At(const void* base, int off) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + off);
}

struct FString {
    TCHAR*  Data = nullptr;
    int32_t Num = 0;  // includes terminator
    int32_t Max = 0;
};

template <typename T>
struct TArray {
    T*      Data;
    int32_t Num;
    int32_t Max;
};

struct FWeakObjectPtr {
    int32_t ObjectIndex;
    int32_t ObjectSerialNumber;
};

struct FText {  // opaque, 0x18
    void*    Data;
    void*    Ref;
    uint32_t Flags;
    uint32_t Pad;
};
static_assert(sizeof(FText) == 0x18, "FText size (ElementSize of TextProperty)");

}  // namespace ue4
