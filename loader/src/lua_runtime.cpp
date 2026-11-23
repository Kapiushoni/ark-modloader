// Lua 5.4 binding for ArkModLoader. See mods/README.md ("Lua mods") for the script-side API.
#include "lua_runtime.h"

#include <time.h>

#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include "dispatch.h"
#include "engine.h"
#include "log.h"
#include "lua.hpp"
#include "ui.h"

namespace luart {
namespace {

using UObj = ue4::UObject;
using ue4::At;

// ------------------------------------------------------------------ script bookkeeping
struct Callback {
    Script* s;
    int ref = LUA_NOREF;   // function in the registry
    int ref2 = LUA_NOREF;  // hooks: post function
};

}  // namespace

struct Script {
    lua_State* L = nullptr;
    std::string id, name, lastError;
    Host host;
    bool dead = false;  // set on unload; the shell is kept so late callbacks can bail out safely
    int errors = 0;
    std::vector<std::unique_ptr<Callback>> callbacks;
    std::vector<std::unique_ptr<int>> toggles;
};

namespace {

int gBusy = 0;
struct BusyGuard {
    BusyGuard() { ++gBusy; }
    ~BusyGuard() { --gBusy; }
};

Script* Self(lua_State* L) { return *static_cast<Script**>(lua_getextraspace(L)); }

double Now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void Report(Script* s, const char* msg) {
    s->lastError = msg ? msg : "?";
    ++s->errors;
    if (s->errors <= 20) LOGE("lua[%s]: %s", s->id.c_str(), s->lastError.c_str());
    if (s->errors == 20) LOGE("lua[%s]: too many errors, further ones are not logged", s->id.c_str());
}

int Traceback(lua_State* L) {
    const char* m = lua_tostring(L, 1);
    luaL_traceback(L, L, m ? m : "(error object is not a string)", 1);
    return 1;
}

// Calls the function below `nargs` arguments on the stack. On error reports it and leaves
// nothing on the stack; on success leaves `nres` results.
bool PCall(Script* s, int nargs, int nres) {
    lua_State* L = s->L;
    int fn = lua_gettop(L) - nargs;
    lua_pushcfunction(L, Traceback);
    lua_insert(L, fn);
    int rc;
    {
        BusyGuard g;
        rc = lua_pcall(L, nargs, nres, fn);
    }
    lua_remove(L, fn);
    if (rc != LUA_OK) {
        Report(s, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ reflection helpers
UObj* Children(UObj* s) { return At<UObj*>(s, ue4::kStruct_Children); }
UObj* Next(UObj* f) { return At<UObj*>(f, ue4::kField_Next); }
int Offset(UObj* p) { return At<int32_t>(p, ue4::kProp_Offset); }
int ElemSize(UObj* p) { return At<int32_t>(p, ue4::kProp_ElementSize); }
int ArrayDim(UObj* p) { return At<int32_t>(p, ue4::kProp_ArrayDim); }
uint64_t Flags(UObj* p) { return At<uint64_t>(p, ue4::kProp_Flags); }
UObj* Extra(UObj* p) { return At<UObj*>(p, ue4::kProp_Extra); }

UObj* FindProp(UObj* strctOrObj, const char* name) {
    UObj* f = engine::FindField(strctOrObj, name);
    return engine::IsProperty(f) ? f : nullptr;
}

// ------------------------------------------------------------------ userdata types
const char* kObjMT = "ark.Object";
const char* kStructMT = "ark.Struct";
const char* kToggleMT = "ark.Toggle";

struct ObjBox {
    UObj* p;
    int32_t idx;
};
struct StructBox {
    uint8_t* base;
    UObj* strct;
    ObjBox owner;   // validity anchor (p == nullptr: hook params, valid during the callback)
};

bool Alive(const ObjBox& b) {
    if (!b.p) return false;
    auto& arr = engine::Objects()->ObjObjects;
    return b.idx >= 0 && b.idx < arr.NumElements && arr.Objects[b.idx].Object == b.p;
}

void PushObj(lua_State* L, UObj* o) {
    if (!o || !engine::IsValid(o)) { lua_pushnil(L); return; }
    auto* b = static_cast<ObjBox*>(lua_newuserdatauv(L, sizeof(ObjBox), 0));
    b->p = o;
    b->idx = o->InternalIndex;
    luaL_setmetatable(L, kObjMT);
}

UObj* CheckObj(lua_State* L, int i) {
    auto* b = static_cast<ObjBox*>(luaL_checkudata(L, i, kObjMT));
    if (!Alive(*b)) luaL_error(L, "object is no longer valid (destroyed or unloaded)");
    return b->p;
}

UObj* OptObjOrPath(lua_State* L, int i) {  // Object, path string, or nil
    if (lua_isnoneornil(L, i)) return nullptr;
    if (lua_type(L, i) == LUA_TSTRING) return engine::FindObject(lua_tostring(L, i));
    return CheckObj(L, i);
}

void PushStruct(lua_State* L, uint8_t* base, UObj* strct, const ObjBox& owner) {
    auto* b = static_cast<StructBox*>(lua_newuserdatauv(L, sizeof(StructBox), 0));
    *b = {base, strct, owner};
    luaL_setmetatable(L, kStructMT);
}

StructBox* CheckStruct(lua_State* L, int i) {
    auto* b = static_cast<StructBox*>(luaL_checkudata(L, i, kStructMT));
    if (b->owner.p && !Alive(b->owner)) luaL_error(L, "struct belongs to an object that is no longer valid");
    return b;
}

// ------------------------------------------------------------------ value conversion
void PushValue(lua_State* L, UObj* prop, uint8_t* addr, const ObjBox& owner, bool copyStructs);

void PushStructCopy(lua_State* L, uint8_t* base, UObj* strct) {
    lua_newtable(L);
    for (UObj* s = strct; s; s = At<UObj*>(s, ue4::kStruct_Super))
        for (UObj* f = Children(s); f; f = Next(f))
            if (engine::IsProperty(f)) {
                PushValue(L, f, base + Offset(f), ObjBox{nullptr, 0}, true);
                lua_setfield(L, -2, engine::Name(f).c_str());
            }
}

void PushValue(lua_State* L, UObj* prop, uint8_t* addr, const ObjBox& owner, bool copyStructs) {
    const std::string t = engine::TypeOf(prop);
    if (t == "FloatProperty") lua_pushnumber(L, *reinterpret_cast<float*>(addr));
    else if (t == "DoubleProperty") lua_pushnumber(L, *reinterpret_cast<double*>(addr));
    else if (t == "IntProperty") lua_pushinteger(L, *reinterpret_cast<int32_t*>(addr));
    else if (t == "Int8Property") lua_pushinteger(L, *reinterpret_cast<int8_t*>(addr));
    else if (t == "Int16Property") lua_pushinteger(L, *reinterpret_cast<int16_t*>(addr));
    else if (t == "Int64Property") lua_pushinteger(L, *reinterpret_cast<int64_t*>(addr));
    else if (t == "UInt16Property") lua_pushinteger(L, *reinterpret_cast<uint16_t*>(addr));
    else if (t == "UInt32Property") lua_pushinteger(L, *reinterpret_cast<uint32_t*>(addr));
    else if (t == "UInt64Property") lua_pushinteger(L, lua_Integer(*reinterpret_cast<uint64_t*>(addr)));
    else if (t == "ByteProperty") lua_pushinteger(L, *addr);
    else if (t == "EnumProperty") {
        int sz = ElemSize(prop);
        uint64_t v = sz == 1 ? *addr : sz == 2 ? *reinterpret_cast<uint16_t*>(addr)
                   : sz == 4 ? *reinterpret_cast<uint32_t*>(addr) : *reinterpret_cast<uint64_t*>(addr);
        lua_pushinteger(L, lua_Integer(v));
    } else if (t == "BoolProperty") {
        uint8_t off = At<uint8_t>(prop, ue4::kBool_ByteOffset), mask = At<uint8_t>(prop, ue4::kBool_ByteMask);
        lua_pushboolean(L, (addr[off] & mask) != 0);
    } else if (t == "ObjectProperty" || t == "ClassProperty") {
        PushObj(L, *reinterpret_cast<UObj**>(addr));
    } else if (t == "WeakObjectProperty") {
        PushObj(L, engine::ResolveWeak(*reinterpret_cast<ue4::FWeakObjectPtr*>(addr)));
    } else if (t == "StructProperty") {
        if (copyStructs) PushStructCopy(L, addr, Extra(prop));
        else PushStruct(L, addr, Extra(prop), owner);
    } else if (t == "StrProperty") {
        auto* s = reinterpret_cast<ue4::FString*>(addr);
        lua_pushstring(L, s->Data ? engine::ToUtf8(s->Data).c_str() : "");
    } else if (t == "NameProperty") {
        lua_pushstring(L, engine::NameToString(*reinterpret_cast<ue4::FName*>(addr)).c_str());
    } else if (t == "TextProperty") {
        lua_pushstring(L, engine::TextToUtf8(*reinterpret_cast<ue4::FText*>(addr)).c_str());
    } else if (t == "ArrayProperty") {
        auto* arr = reinterpret_cast<ue4::TArray<uint8_t>*>(addr);
        UObj* inner = Extra(prop);
        int esz = ElemSize(inner);
        lua_createtable(L, arr->Num, 0);
        for (int i = 0; i < arr->Num && arr->Data; ++i) {
            PushValue(L, inner, arr->Data + i * esz, owner, true);  // copies: the array may reallocate
            lua_rawseti(L, -2, i + 1);
        }
    } else {
        lua_pushnil(L);
    }
}

void WriteValue(lua_State* L, int vi, UObj* prop, uint8_t* addr, std::deque<std::wstring>* temps) {
    vi = lua_absindex(L, vi);
    const std::string t = engine::TypeOf(prop);
    if (t == "FloatProperty") *reinterpret_cast<float*>(addr) = float(luaL_checknumber(L, vi));
    else if (t == "DoubleProperty") *reinterpret_cast<double*>(addr) = luaL_checknumber(L, vi);
    else if (t == "IntProperty") *reinterpret_cast<int32_t*>(addr) = int32_t(luaL_checkinteger(L, vi));
    else if (t == "Int8Property") *reinterpret_cast<int8_t*>(addr) = int8_t(luaL_checkinteger(L, vi));
    else if (t == "Int16Property") *reinterpret_cast<int16_t*>(addr) = int16_t(luaL_checkinteger(L, vi));
    else if (t == "Int64Property") *reinterpret_cast<int64_t*>(addr) = luaL_checkinteger(L, vi);
    else if (t == "UInt16Property") *reinterpret_cast<uint16_t*>(addr) = uint16_t(luaL_checkinteger(L, vi));
    else if (t == "UInt32Property") *reinterpret_cast<uint32_t*>(addr) = uint32_t(luaL_checkinteger(L, vi));
    else if (t == "UInt64Property") *reinterpret_cast<uint64_t*>(addr) = uint64_t(luaL_checkinteger(L, vi));
    else if (t == "ByteProperty") *addr = uint8_t(luaL_checkinteger(L, vi));
    else if (t == "EnumProperty") {
        uint64_t v = uint64_t(luaL_checkinteger(L, vi));
        std::memcpy(addr, &v, std::min(ElemSize(prop), 8));
    } else if (t == "BoolProperty") {
        uint8_t off = At<uint8_t>(prop, ue4::kBool_ByteOffset), mask = At<uint8_t>(prop, ue4::kBool_ByteMask);
        if (lua_toboolean(L, vi)) addr[off] |= mask;
        else addr[off] &= uint8_t(~mask);
    } else if (t == "ObjectProperty" || t == "ClassProperty") {
        *reinterpret_cast<UObj**>(addr) = lua_isnil(L, vi) ? nullptr : CheckObj(L, vi);
    } else if (t == "StructProperty") {
        UObj* st = Extra(prop);
        if (lua_istable(L, vi)) {
            lua_pushnil(L);
            while (lua_next(L, vi)) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    UObj* f = FindProp(st, lua_tostring(L, -2));
                    if (!f) luaL_error(L, "struct %s has no field '%s'", engine::Name(st).c_str(), lua_tostring(L, -2));
                    WriteValue(L, -1, f, addr + Offset(f), temps);
                }
                lua_pop(L, 1);
            }
        } else {
            StructBox* b = CheckStruct(L, vi);
            if (b->strct != st) luaL_error(L, "expected struct %s", engine::Name(st).c_str());
            std::memmove(addr, b->base, ElemSize(prop));
        }
    } else if (t == "StrProperty") {
        if (!temps) luaL_error(L, "strings can only be passed as function arguments");
        temps->push_back(engine::FromUtf8(luaL_checkstring(L, vi)));
        ue4::FString f;
        f.Data = temps->back().data();
        f.Num = f.Max = int32_t(temps->back().size() + 1);
        std::memcpy(addr, &f, sizeof f);
    } else if (t == "NameProperty") {
        ue4::FName n = engine::MakeName(luaL_checkstring(L, vi));
        std::memcpy(addr, &n, sizeof n);
    } else if (t == "TextProperty") {
        ue4::FText x = engine::MakeText(luaL_checkstring(L, vi));
        std::memcpy(addr, &x, sizeof x);
    } else {
        luaL_error(L, "cannot assign to a %s", t.c_str());
    }
}

// Address of element `idx` of property `prop` inside `base`.
uint8_t* PropAddr(lua_State* L, uint8_t* base, UObj* prop, lua_Integer idx) {
    if (idx < 0 || idx >= ArrayDim(prop)) luaL_error(L, "index %d out of range (0..%d)", int(idx), ArrayDim(prop) - 1);
    return base + Offset(prop) + idx * ElemSize(prop);
}

// Pushes property value; static arrays without an index come back as a table (copy).
void GetProp(lua_State* L, uint8_t* base, UObj* prop, const ObjBox& owner, int idxArg) {
    if (ArrayDim(prop) > 1 && lua_isnoneornil(L, idxArg)) {
        lua_createtable(L, ArrayDim(prop), 0);
        for (int i = 0; i < ArrayDim(prop); ++i) {
            PushValue(L, prop, base + Offset(prop) + i * ElemSize(prop), owner, true);
            lua_rawseti(L, -2, i);  // 0-based, like the engine
        }
        return;
    }
    PushValue(L, prop, PropAddr(L, base, prop, luaL_optinteger(L, idxArg, 0)), owner, false);
}

void SetProp(lua_State* L, uint8_t* base, UObj* prop, int valArg, int idxArg) {
    if (ArrayDim(prop) > 1 && lua_isnoneornil(L, idxArg) && lua_istable(L, valArg)) {
        for (int i = 0; i < ArrayDim(prop); ++i) {
            lua_rawgeti(L, valArg, i);
            if (!lua_isnil(L, -1)) WriteValue(L, -1, prop, base + Offset(prop) + i * ElemSize(prop), nullptr);
            lua_pop(L, 1);
        }
        return;
    }
    WriteValue(L, valArg, prop, PropAddr(L, base, prop, luaL_optinteger(L, idxArg, 0)), nullptr);
}

// ------------------------------------------------------------------ calling UFunctions
std::vector<UObj*> Params(UObj* func) {
    std::vector<UObj*> out;
    for (UObj* f = Children(func); f; f = Next(f))
        if (engine::IsProperty(f) && (Flags(f) & ue4::CPF_Parm)) out.push_back(f);
    return out;
}

// obj:call(name_or_func, args...) — args either positional or one table of named params.
// Returns ReturnValue (or nil) and a table with every parameter after the call.
int CallFunction(lua_State* L, UObj* obj, UObj* func, int first) {
    int size = engine::ParamsSize(func);
    std::vector<uint64_t> storage(size / 8 + 4, 0);
    auto* buf = reinterpret_cast<uint8_t*>(storage.data());
    std::deque<std::wstring> temps;
    std::vector<UObj*> params = Params(func);
    int nargs = lua_gettop(L) - first + 1;

    bool named = false;
    if (nargs == 1 && lua_istable(L, first))
        for (UObj* p : params) {
            lua_getfield(L, first, engine::Name(p).c_str());
            named |= !lua_isnil(L, -1);
            lua_pop(L, 1);
        }
    int next = first;
    for (UObj* p : params) {
        if (Flags(p) & ue4::CPF_ReturnParm) continue;
        if (named) {
            lua_getfield(L, first, engine::Name(p).c_str());
            if (!lua_isnil(L, -1)) WriteValue(L, -1, p, buf + Offset(p), &temps);
            lua_pop(L, 1);
        } else if (next < first + nargs) {
            WriteValue(L, next++, p, buf + Offset(p), &temps);
        }
    }

    dispatch::CallOriginal(obj, func, buf);

    UObj* ret = nullptr;
    for (UObj* p : params) if (Flags(p) & ue4::CPF_ReturnParm) ret = p;
    if (ret) PushValue(L, ret, buf + Offset(ret), ObjBox{nullptr, 0}, true);
    else lua_pushnil(L);
    lua_newtable(L);
    for (UObj* p : params) {
        if (Flags(p) & (ue4::CPF_ReturnParm | ue4::CPF_Parm)) {
            PushValue(L, p, buf + Offset(p), ObjBox{nullptr, 0}, true);
            lua_setfield(L, -2, engine::Name(p).c_str());
        }
    }
    return 2;
}

int BoundCall(lua_State* L) {  // upvalue 1: UFunction* ; arg 1: self object
    auto* func = static_cast<UObj*>(lua_touserdata(L, lua_upvalueindex(1)));
    UObj* obj = CheckObj(L, 1);
    return CallFunction(L, obj, func, 2);
}

// ------------------------------------------------------------------ ark.Object
int ObjValid(lua_State* L) {
    auto* b = static_cast<ObjBox*>(luaL_checkudata(L, 1, kObjMT));
    lua_pushboolean(L, Alive(*b));
    return 1;
}
int ObjName(lua_State* L) { lua_pushstring(L, engine::Name(CheckObj(L, 1)).c_str()); return 1; }
int ObjPath(lua_State* L) { lua_pushstring(L, engine::Path(CheckObj(L, 1)).c_str()); return 1; }
int ObjClass(lua_State* L) { PushObj(L, reinterpret_cast<UObj*>(CheckObj(L, 1)->ClassPrivate)); return 1; }
int ObjClassName(lua_State* L) {
    lua_pushstring(L, engine::Name(reinterpret_cast<UObj*>(CheckObj(L, 1)->ClassPrivate)).c_str());
    return 1;
}
int ObjIsA(lua_State* L) {
    UObj* o = CheckObj(L, 1);
    UObj* c = OptObjOrPath(L, 2);
    lua_pushboolean(L, c && engine::IsA(o, c));
    return 1;
}
int ObjGet(lua_State* L) {
    auto* b = static_cast<ObjBox*>(luaL_checkudata(L, 1, kObjMT));
    UObj* o = CheckObj(L, 1);
    UObj* p = FindProp(o, luaL_checkstring(L, 2));
    if (!p) return luaL_error(L, "%s has no property '%s'", engine::Name(reinterpret_cast<UObj*>(o->ClassPrivate)).c_str(), lua_tostring(L, 2));
    GetProp(L, reinterpret_cast<uint8_t*>(o), p, *b, 3);
    return 1;
}
int ObjSet(lua_State* L) {
    UObj* o = CheckObj(L, 1);
    UObj* p = FindProp(o, luaL_checkstring(L, 2));
    if (!p) return luaL_error(L, "%s has no property '%s'", engine::Name(reinterpret_cast<UObj*>(o->ClassPrivate)).c_str(), lua_tostring(L, 2));
    SetProp(L, reinterpret_cast<uint8_t*>(o), p, 3, 4);
    return 0;
}
int ObjCall(lua_State* L) {
    UObj* o = CheckObj(L, 1);
    UObj* f = engine::FindField(o, luaL_checkstring(L, 2));
    if (!engine::IsFunction(f)) return luaL_error(L, "%s has no function '%s'", engine::Name(reinterpret_cast<UObj*>(o->ClassPrivate)).c_str(), lua_tostring(L, 2));
    return CallFunction(L, o, f, 3);
}

const luaL_Reg kObjMethods[] = {
    {"valid", ObjValid}, {"name", ObjName}, {"path", ObjPath}, {"class", ObjClass},
    {"class_name", ObjClassName}, {"is_a", ObjIsA}, {"get", ObjGet}, {"set", ObjSet}, {"call", ObjCall},
    {nullptr, nullptr}};

int ObjIndex(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    for (const luaL_Reg* r = kObjMethods; r->name; ++r)
        if (!strcmp(r->name, key)) { lua_pushcfunction(L, r->func); return 1; }
    auto* b = static_cast<ObjBox*>(luaL_checkudata(L, 1, kObjMT));
    UObj* o = CheckObj(L, 1);
    UObj* f = engine::FindField(o, key);
    if (engine::IsProperty(f)) {
        GetProp(L, reinterpret_cast<uint8_t*>(o), f, *b, 3);  // arg 3 is none -> element 0 / whole array
        return 1;
    }
    if (engine::IsFunction(f)) {
        lua_pushlightuserdata(L, f);
        lua_pushcclosure(L, BoundCall, 1);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}
int ObjNewIndex(lua_State* L) {
    UObj* o = CheckObj(L, 1);
    UObj* p = FindProp(o, luaL_checkstring(L, 2));
    if (!p) return luaL_error(L, "%s has no property '%s'", engine::Name(reinterpret_cast<UObj*>(o->ClassPrivate)).c_str(), lua_tostring(L, 2));
    SetProp(L, reinterpret_cast<uint8_t*>(o), p, 3, 4);
    return 0;
}
int ObjEq(lua_State* L) {
    auto* a = static_cast<ObjBox*>(luaL_checkudata(L, 1, kObjMT));
    auto* b = static_cast<ObjBox*>(luaL_checkudata(L, 2, kObjMT));
    lua_pushboolean(L, a->p == b->p);
    return 1;
}
int ObjToString(lua_State* L) {
    auto* b = static_cast<ObjBox*>(luaL_checkudata(L, 1, kObjMT));
    if (!Alive(*b)) lua_pushstring(L, "UObject(invalid)");
    else lua_pushfstring(L, "UObject(%s)", engine::ClassAndPath(b->p).c_str());
    return 1;
}

// ------------------------------------------------------------------ ark.Struct
int StructIndex(lua_State* L) {
    StructBox* b = CheckStruct(L, 1);
    UObj* p = FindProp(b->strct, luaL_checkstring(L, 2));
    if (!p) { lua_pushnil(L); return 1; }
    GetProp(L, b->base, p, b->owner, 3);
    return 1;
}
int StructNewIndex(lua_State* L) {
    StructBox* b = CheckStruct(L, 1);
    UObj* p = FindProp(b->strct, luaL_checkstring(L, 2));
    if (!p) return luaL_error(L, "struct %s has no field '%s'", engine::Name(b->strct).c_str(), lua_tostring(L, 2));
    SetProp(L, b->base, p, 3, 4);
    return 0;
}
int StructToString(lua_State* L) {
    StructBox* b = CheckStruct(L, 1);
    lua_pushfstring(L, "UStruct(%s)", engine::Name(b->strct).c_str());
    return 1;
}

// ------------------------------------------------------------------ ark.Toggle
int ToggleIndex(lua_State* L) {
    int* v = *static_cast<int**>(luaL_checkudata(L, 1, kToggleMT));
    if (!strcmp(luaL_checkstring(L, 2), "on")) lua_pushboolean(L, *v != 0);
    else lua_pushnil(L);
    return 1;
}
int ToggleNewIndex(lua_State* L) {
    int* v = *static_cast<int**>(luaL_checkudata(L, 1, kToggleMT));
    if (strcmp(luaL_checkstring(L, 2), "on")) return luaL_error(L, "toggle only has the field 'on'");
    *v = lua_toboolean(L, 3) ? 1 : 0;
    return 0;
}

// ------------------------------------------------------------------ trampolines (game thread)
void TickTramp(void* u) {
    auto* c = static_cast<Callback*>(u);
    if (c->s->dead) return;
    lua_rawgeti(c->s->L, LUA_REGISTRYINDEX, c->ref);
    PCall(c->s, 0, 0);
}
void DrawTramp(void* u, float w, float h) {
    auto* c = static_cast<Callback*>(u);
    if (c->s->dead) return;
    lua_State* L = c->s->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, c->ref);
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    PCall(c->s, 2, 0);
}
int TouchTramp(void* u, int phase, int finger, float x, float y) {
    auto* c = static_cast<Callback*>(u);
    if (c->s->dead) return 0;
    lua_State* L = c->s->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, c->ref);
    lua_pushinteger(L, phase);
    lua_pushinteger(L, finger);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    if (!PCall(c->s, 4, 1)) return 0;
    int taken = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return taken;
}
// Hook callbacks get (self, params); params fields are accessible like a struct.
bool HookCall(Callback* c, int ref, ArkHookCtx* ctx, bool wantResult) {
    lua_State* L = c->s->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    PushObj(L, reinterpret_cast<UObj*>(ctx->self));
    PushStruct(L, static_cast<uint8_t*>(ctx->params), reinterpret_cast<UObj*>(ctx->func), ObjBox{nullptr, 0});
    if (!PCall(c->s, 2, 1)) return true;
    bool allow = !(wantResult && lua_isboolean(L, -1) && !lua_toboolean(L, -1));
    lua_pop(L, 1);
    return allow;
}
int PreTramp(ArkHookCtx* ctx) {
    if (!dispatch::OnGameThread()) return 1;  // a lua_State must only be used from one thread
    auto* c = static_cast<Callback*>(ctx->user);
    if (c->s->dead || c->ref == LUA_NOREF) return 1;
    return HookCall(c, c->ref, ctx, true) ? 1 : 0;
}
void PostTramp(ArkHookCtx* ctx) {
    if (!dispatch::OnGameThread()) return;
    auto* c = static_cast<Callback*>(ctx->user);
    if (c->s->dead || c->ref2 == LUA_NOREF) return;
    HookCall(c, c->ref2, ctx, false);
}

Callback* NewCallback(lua_State* L, int fnArg) {
    Script* s = Self(L);
    luaL_checktype(L, fnArg, LUA_TFUNCTION);
    auto c = std::make_unique<Callback>();
    c->s = s;
    lua_pushvalue(L, fnArg);
    c->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    s->callbacks.push_back(std::move(c));
    return s->callbacks.back().get();
}

// ------------------------------------------------------------------ the `ark` table
int ArkLog(lua_State* L) {
    std::string msg;
    for (int i = 1, n = lua_gettop(L); i <= n; ++i) {
        if (i > 1) msg += ' ';
        msg += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    LOG("[lua:%s] %s", Self(L)->id.c_str(), msg.c_str());
    return 0;
}
int ArkConfig(lua_State* L) {
    Script* s = Self(L);
    const char* key = luaL_checkstring(L, 1);
    int t = lua_type(L, 2);
    std::string def = t == LUA_TBOOLEAN ? (lua_toboolean(L, 2) ? "true" : "false")
                    : t == LUA_TNONE || t == LUA_TNIL ? "" : luaL_tolstring(L, 2, nullptr);
    std::string v = s->host.config(key, def);
    if (t == LUA_TNUMBER) {
        lua_settop(L, 2);
        if (lua_stringtonumber(L, v.c_str()) == 0) lua_pushvalue(L, 2);
    } else if (t == LUA_TBOOLEAN) {
        lua_pushboolean(L, v == "1" || v == "true" || v == "on" || v == "yes");
    } else {
        lua_pushstring(L, v.c_str());
    }
    return 1;
}
int ArkToggle(lua_State* L) {
    Script* s = Self(L);
    const char* label = luaL_checkstring(L, 1);
    s->toggles.push_back(std::make_unique<int>(lua_toboolean(L, 2) ? 1 : 0));
    int* v = s->toggles.back().get();
    ui::AddToggle(label, v, s);
    *static_cast<int**>(lua_newuserdatauv(L, sizeof(int*), 0)) = v;
    luaL_setmetatable(L, kToggleMT);
    return 1;
}
int ArkOnTick(lua_State* L) { dispatch::AddTick(TickTramp, NewCallback(L, 1), Self(L)); return 0; }
int ArkOnDraw(lua_State* L) { ui::AddDraw(DrawTramp, NewCallback(L, 1), Self(L)); return 0; }
int ArkOnTouch(lua_State* L) { ui::AddTouch(TouchTramp, NewCallback(L, 1), Self(L)); return 0; }
int ArkHook(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    bool pre = !lua_isnoneornil(L, 2), post = !lua_isnoneornil(L, 3);
    if (!pre && !post) return luaL_error(L, "ark.hook needs a pre and/or post function");
    Script* s = Self(L);
    auto c = std::make_unique<Callback>();
    c->s = s;
    if (pre) { luaL_checktype(L, 2, LUA_TFUNCTION); lua_pushvalue(L, 2); c->ref = luaL_ref(L, LUA_REGISTRYINDEX); }
    if (post) { luaL_checktype(L, 3, LUA_TFUNCTION); lua_pushvalue(L, 3); c->ref2 = luaL_ref(L, LUA_REGISTRYINDEX); }
    s->callbacks.push_back(std::move(c));
    dispatch::AddHook(path, pre ? PreTramp : nullptr, post ? PostTramp : nullptr, s->callbacks.back().get(), s);
    return 0;
}
int ArkDrawRect(lua_State* L) {
    ui::DrawRect(float(luaL_checknumber(L, 1)), float(luaL_checknumber(L, 2)), float(luaL_checknumber(L, 3)),
                 float(luaL_checknumber(L, 4)), float(luaL_optnumber(L, 5, 1)), float(luaL_optnumber(L, 6, 1)),
                 float(luaL_optnumber(L, 7, 1)), float(luaL_optnumber(L, 8, 1)));
    return 0;
}
int ArkDrawText(lua_State* L) {
    ui::DrawText(luaL_checkstring(L, 1), float(luaL_checknumber(L, 2)), float(luaL_checknumber(L, 3)),
                 float(luaL_optnumber(L, 4, 1)), float(luaL_optnumber(L, 5, 1)), float(luaL_optnumber(L, 6, 1)),
                 float(luaL_optnumber(L, 7, 1)));
    return 0;
}
int ArkFind(lua_State* L) { PushObj(L, engine::FindObject(luaL_checkstring(L, 1))); return 1; }
int ArkWorld(lua_State* L) { PushObj(L, engine::World()); return 1; }
int ArkWorldName(lua_State* L) { lua_pushstring(L, engine::Name(engine::World()).c_str()); return 1; }
int ArkController(lua_State* L) { PushObj(L, engine::LocalPlayerController()); return 1; }
int ArkPlayer(lua_State* L) {
    UObj* pc = engine::LocalPlayerController();
    static int pawnOff = -1;
    if (pc && pawnOff < 0) pawnOff = engine::PropertyOffset(engine::FindObject("/Script/Engine.Controller"), "Pawn");
    PushObj(L, pc && pawnOff >= 0 ? At<UObj*>(pc, pawnOff) : nullptr);
    return 1;
}
int ArkObjects(lua_State* L) {
    UObj* cls = OptObjOrPath(L, 1);
    if (!cls) return luaL_error(L, "ark.objects: class not found");
    lua_Integer limit = luaL_optinteger(L, 2, 10000);
    lua_newtable(L);
    auto& arr = engine::Objects()->ObjObjects;
    lua_Integer n = 0;
    for (int i = 0; i < arr.NumElements && n < limit; ++i) {
        UObj* o = arr.Objects[i].Object;
        if (!o || engine::IsCDO(o) || !engine::IsA(o, cls)) continue;
        PushObj(L, o);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
int ArkTime(lua_State* L) { lua_pushnumber(L, Now()); return 1; }

const luaL_Reg kArk[] = {
    {"log", ArkLog}, {"config", ArkConfig}, {"toggle", ArkToggle},
    {"on_tick", ArkOnTick}, {"on_draw", ArkOnDraw}, {"on_touch", ArkOnTouch}, {"hook", ArkHook},
    {"draw_rect", ArkDrawRect}, {"draw_text", ArkDrawText},
    {"find", ArkFind}, {"world", ArkWorld}, {"world_name", ArkWorldName},
    {"controller", ArkController}, {"player", ArkPlayer}, {"objects", ArkObjects}, {"time", ArkTime},
    {nullptr, nullptr}};

void SetupState(lua_State* L, const Host& host) {
    luaL_openlibs(L);

    luaL_newmetatable(L, kObjMT);
    lua_pushcfunction(L, ObjIndex); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, ObjNewIndex); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, ObjEq); lua_setfield(L, -2, "__eq");
    lua_pushcfunction(L, ObjToString); lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    luaL_newmetatable(L, kStructMT);
    lua_pushcfunction(L, StructIndex); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, StructNewIndex); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, StructToString); lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    luaL_newmetatable(L, kToggleMT);
    lua_pushcfunction(L, ToggleIndex); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, ToggleNewIndex); lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    luaL_newlib(L, kArk);
    lua_pushstring(L, host.modsDir.c_str());
    lua_setfield(L, -2, "mods_dir");
    lua_setglobal(L, "ark");

    lua_pushcfunction(L, ArkLog);
    lua_setglobal(L, "print");
}

}  // namespace

Script* Load(const std::string& path, const std::string& id, Host host, std::string* name, std::string* err) {
    auto* s = new Script;
    s->id = id;
    s->host = std::move(host);
    s->L = luaL_newstate();
    if (!s->L) { *err = "out of memory"; delete s; return nullptr; }
    *static_cast<Script**>(lua_getextraspace(s->L)) = s;
    SetupState(s->L, s->host);

    if (luaL_loadfilex(s->L, path.c_str(), "t") != LUA_OK) {
        *err = lua_tostring(s->L, -1);
        lua_close(s->L);
        delete s;
        return nullptr;
    }
    if (!PCall(s, 0, 0)) {
        *err = s->lastError;
        Unload(s);  // drop whatever it registered before failing
        return nullptr;
    }
    lua_getglobal(s->L, "MOD_NAME");
    s->name = lua_isstring(s->L, -1) ? lua_tostring(s->L, -1) : id;
    lua_pop(s->L, 1);
    *name = s->name;
    return s;
}

void Unload(Script* s) {
    if (!s || s->dead) return;
    lua_getglobal(s->L, "shutdown");
    if (lua_isfunction(s->L, -1)) PCall(s, 0, 0);
    else lua_pop(s->L, 1);
    dispatch::RemoveOwner(s);
    ui::RemoveOwner(s);
    lua_close(s->L);
    s->L = nullptr;
    s->dead = true;
    // The Script shell (callbacks, toggles) is intentionally kept: a hook list snapshot on the
    // stack may still hand us one of its callbacks; they check `dead` and return.
}

bool AnyBusy() { return gBusy > 0; }

int ErrorCount(const Script* s) { return s ? s->errors : 0; }

}  // namespace luart
