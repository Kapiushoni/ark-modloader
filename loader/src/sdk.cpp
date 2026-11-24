#include "sdk.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

#include "engine.h"
#include "log.h"

namespace sdk {
namespace {

using engine::UObject;
using ue4::At;

// Layouts verified against libUE4.so (UEnum::GetNameByIndex, UObject::ProcessEvent).
constexpr int kEnum_Names = 0x40;          // TArray<TPair<FName, int64>>
constexpr int kFunc_Flags = 0x88;          // UFunction::FunctionFlags
constexpr int kMap_ValueProp = 0x80;       // UMapProperty::ValueProp (KeyProp at kProp_Extra)
constexpr uint32_t FUNC_Static = 0x2000, FUNC_Native = 0x400, FUNC_Event = 0x800,
                   FUNC_BlueprintCallable = 0x4000000, FUNC_Const = 0x40000000;
constexpr uint64_t CPF_ConstParm = 0x2;

UObject* Super(UObject* s) { return At<UObject*>(s, ue4::kStruct_Super); }
UObject* Children(UObject* s) { return At<UObject*>(s, ue4::kStruct_Children); }
UObject* Next(UObject* f) { return At<UObject*>(f, ue4::kField_Next); }
UObject* Extra(UObject* p, int off = ue4::kProp_Extra) { return At<UObject*>(p, off); }

std::string Lower(std::string s) {
    for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string TypeName(UObject* p) {
    const std::string t = engine::TypeOf(p);
    auto name = [](UObject* o) { return o ? engine::Name(o) : std::string("?"); };
    if (t == "FloatProperty") return "float";
    if (t == "DoubleProperty") return "double";
    if (t == "IntProperty") return "int32";
    if (t == "Int8Property") return "int8";
    if (t == "Int16Property") return "int16";
    if (t == "Int64Property") return "int64";
    if (t == "ByteProperty") return Extra(p) ? "enum " + name(Extra(p)) + " (uint8)" : "uint8";
    if (t == "UInt16Property") return "uint16";
    if (t == "UInt32Property") return "uint32";
    if (t == "UInt64Property") return "uint64";
    if (t == "BoolProperty") return "bool";
    if (t == "StrProperty") return "FString";
    if (t == "NameProperty") return "FName";
    if (t == "TextProperty") return "FText";
    if (t == "EnumProperty") return "enum " + name(At<UObject*>(p, ue4::kProp_Extra + 8));
    if (t == "StructProperty") return "struct " + name(Extra(p));
    if (t == "ObjectProperty") return name(Extra(p)) + "*";
    if (t == "ClassProperty") return "TSubclassOf<" + name(At<UObject*>(p, ue4::kProp_Extra + 8)) + ">";
    if (t == "WeakObjectProperty") return "TWeakObjectPtr<" + name(Extra(p)) + ">";
    if (t == "LazyObjectProperty") return "TLazyObjectPtr<" + name(Extra(p)) + ">";
    if (t == "AssetObjectProperty" || t == "SoftObjectProperty") return "TAssetPtr<" + name(Extra(p)) + ">";
    if (t == "AssetClassProperty" || t == "SoftClassProperty") return "TAssetSubclassOf<?>";
    if (t == "InterfaceProperty") return "TScriptInterface<" + name(Extra(p)) + ">";
    if (t == "ArrayProperty") return Extra(p) ? "TArray<" + TypeName(Extra(p)) + ">" : "TArray<?>";
    if (t == "SetProperty") return Extra(p) ? "TSet<" + TypeName(Extra(p)) + ">" : "TSet<?>";
    if (t == "MapProperty") {
        UObject *k = Extra(p), *v = Extra(p, kMap_ValueProp);
        return "TMap<" + (k ? TypeName(k) : "?") + ", " + (v ? TypeName(v) : "?") + ">";
    }
    if (t == "DelegateProperty") return "delegate";
    if (t == "MulticastDelegateProperty") return "multicast delegate";
    return t;
}

std::string Hex(int v) {
    char b[16];
    snprintf(b, sizeof b, "0x%04x", v);
    return b;
}

void WriteFields(FILE* f, UObject* strct) {
    for (UObject* p = Children(strct); p; p = Next(p)) {
        if (!engine::IsProperty(p)) continue;
        int off = At<int32_t>(p, ue4::kProp_Offset), size = At<int32_t>(p, ue4::kProp_ElementSize);
        int dim = At<int32_t>(p, ue4::kProp_ArrayDim);
        std::string type = TypeName(p);
        std::string extra;
        if (engine::TypeOf(p) == "BoolProperty") {
            char b[32];
            snprintf(b, sizeof b, "  (mask 0x%02x)", At<uint8_t>(p, ue4::kBool_ByteMask));
            extra = b;
        }
        fprintf(f, "    %s  %-40s %s%s;  // size 0x%x%s\n", Hex(off).c_str(), type.c_str(), engine::Name(p).c_str(),
                dim > 1 ? ("[" + std::to_string(dim) + "]").c_str() : "", size * std::max(dim, 1), extra.c_str());
    }
}

void WriteFunctions(FILE* f, UObject* cls) {
    static UObject* functionClass = engine::FindObject("/Script/CoreUObject.Function");
    for (UObject* fn = Children(cls); fn; fn = Next(fn)) {
        if (!engine::IsA(fn, functionClass)) continue;
        uint32_t flags = At<uint32_t>(fn, kFunc_Flags);
        std::string ret = "void", params;
        for (UObject* p = Children(fn); p; p = Next(p)) {
            if (!engine::IsProperty(p)) continue;
            uint64_t pf = At<uint64_t>(p, ue4::kProp_Flags);
            if (!(pf & ue4::CPF_Parm)) continue;
            if (pf & ue4::CPF_ReturnParm) { ret = TypeName(p); continue; }
            if (!params.empty()) params += ", ";
            if ((pf & ue4::CPF_OutParm) && !(pf & CPF_ConstParm)) params += "out ";
            params += TypeName(p) + " " + engine::Name(p);
        }
        std::string tags;
        if (flags & FUNC_Static) tags += " static";
        if (flags & FUNC_Native) tags += " native";
        if (flags & FUNC_Event) tags += " event";
        if (flags & FUNC_BlueprintCallable) tags += " callable";
        fprintf(f, "    fn %s %s(%s)%s;  //%s params 0x%x\n", ret.c_str(), engine::Name(fn).c_str(), params.c_str(),
                (flags & FUNC_Const) ? " const" : "", tags.c_str(), engine::ParamsSize(fn));
    }
}

void WriteEnum(FILE* f, UObject* e) {
    auto& names = At<ue4::TArray<uint8_t>>(e, kEnum_Names);
    fprintf(f, "enum %s {  // %s\n", engine::Name(e).c_str(), engine::Path(e).c_str());
    for (int i = 0; i < names.Num && i < 4096; ++i) {
        const uint8_t* item = names.Data + i * 16;
        const auto& n = *reinterpret_cast<const ue4::FName*>(item);
        long long v = *reinterpret_cast<const int64_t*>(item + 8);
        fprintf(f, "    %s = %lld,\n", engine::NameToString(n).c_str(), v);
    }
    fprintf(f, "};\n\n");
}

}  // namespace

Result Dump(const std::string& dir, const std::string& filter) {
    Result r;
    UObject* classClass = engine::FindObject("/Script/CoreUObject.Class");
    UObject* structClass = engine::FindObject("/Script/CoreUObject.ScriptStruct");
    UObject* enumClass = engine::FindObject("/Script/CoreUObject.Enum");
    if (!classClass || !structClass || !enumClass) { r.error = "reflection classes not found"; return r; }

    // Collect first (sorted by path), then write: the output is stable and easy to diff.
    struct Item { std::string path; UObject* obj; int kind; };  // 0 class, 1 struct, 2 enum
    std::vector<Item> items;
    const std::string want = Lower(filter);
    auto& arr = engine::Objects()->ObjObjects;
    for (int i = 0; i < arr.NumElements; ++i) {
        UObject* o = arr.Objects[i].Object;
        if (!o) continue;
        int kind = engine::IsA(o, classClass) ? 0 : engine::IsA(o, structClass) ? 1 : engine::IsA(o, enumClass) ? 2 : -1;
        if (kind < 0) continue;
        std::string path = engine::Path(o);
        if (!want.empty() && Lower(path).find(want) == std::string::npos) continue;
        items.push_back({path, o, kind});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.path < b.path; });

    mkdir(dir.c_str(), 0770);
    std::string safe;
    for (char c : filter) safe += isalnum(static_cast<unsigned char>(c)) ? c : '_';
    r.path = dir + (safe.empty() ? "/sdk.txt" : "/sdk_" + safe + ".txt");
    FILE* f = fopen(r.path.c_str(), "w");
    if (!f) { r.error = "cannot write " + r.path; return r; }
    fprintf(f, "// ARK Mobile SDK dump by ArkModLoader (UE 4.17, arm64). Offsets are from the start of the object.\n"
               "// Names are what the loader API and Lua use: obj.FieldName, obj:FunctionName(...),\n"
               "// ark.find(\"/Script/Package.Class\"). filter: \"%s\"\n\n", filter.c_str());
    for (const Item& it : items) {
        if (it.kind == 2) { WriteEnum(f, it.obj); ++r.enums; continue; }
        UObject* super = Super(it.obj);
        fprintf(f, "%s %s%s%s {  // %s, size 0x%x\n", it.kind == 0 ? "class" : "struct", engine::Name(it.obj).c_str(),
                super ? " : " : "", super ? engine::Name(super).c_str() : "", it.path.c_str(),
                At<int32_t>(it.obj, ue4::kStruct_PropsSize));
        WriteFields(f, it.obj);
        if (it.kind == 0) WriteFunctions(f, it.obj);
        fprintf(f, "};\n\n");
        ++(it.kind == 0 ? r.classes : r.structs);
    }
    fclose(f);
    r.ok = true;
    LOG("sdk: %d classes, %d structs, %d enums -> %s", r.classes, r.structs, r.enums, r.path.c_str());
    return r;
}

}  // namespace sdk
