// Engine access layer: symbols, names, objects, reflection, text helpers.
#pragma once
#include <string>

#include "elfsym.h"
#include "ue4.h"

namespace engine {

using ue4::FText;
using ue4::FUObjectArray;
using ue4::TCHAR;
using ue4::UObject;

bool Init(void* libue4, const elfsym::Image* image = nullptr);  // resolve symbols (libUE4 handle, or its ELF image)
bool Ready();                     // names + objects populated
void* Sym(const char* name);

FUObjectArray* Objects();
UObject* World();
bool IsMainMenu();
uint32_t GameThreadId();
uint64_t FrameCounter();

std::string Name(UObject* o);
std::string Path(UObject* o);     // "/Script/Engine.Actor.ReceiveBeginPlay"
std::string ClassAndPath(UObject* o);

UObject* FindObject(const char* path);
bool IsA(UObject* o, UObject* cls);
bool IsValid(UObject* o);              // pointer is a live entry of GUObjectArray
UObject* ResolveWeak(const ue4::FWeakObjectPtr& w);
std::string NameToString(const ue4::FName& n);
ue4::FName MakeName(const char* utf8);  // FName(..., FNAME_Add)
std::string TypeOf(UObject* prop);     // "FloatProperty", "StructProperty", ...
bool IsProperty(UObject* field);
bool IsFunction(UObject* field);
UObject* LocalPlayerController();
bool IsCDO(UObject* o);

UObject* FindField(UObject* strct, const char* name);   // walks supers
int PropertyOffset(UObject* strct, const char* name);
int ParamsSize(UObject* func);

std::string ToUtf8(const TCHAR* s);
std::wstring FromUtf8(const char* s);
FText MakeText(const char* utf8);
std::string TextToUtf8(const FText& t);
bool ExecCommand(const char* cmd);

}  // namespace engine
