// Lua 5.4 mods: every ArkMods/*.lua file runs in its own lua_State with the `ark` API.
// All script code runs on the game thread.
#pragma once
#include <functional>
#include <string>

namespace luart {

struct Host {
    // Settings lookup for this script (ArkMods/<id>.ini, same mechanism as C mods).
    std::function<std::string(const std::string& key, const std::string& def)> config;
    std::string modsDir;
};

struct Script;

// Runs the file. On success returns the script (its address is the owner of everything it
// registers); `name` gets MOD_NAME or the id. On failure returns nullptr and fills `err`.
Script* Load(const std::string& path, const std::string& id, Host host, std::string* name, std::string* err);

// Calls the script's shutdown(), drops its registrations and closes the state.
void Unload(Script* s);

// Number of errors the script has hit so far (shown in the MODS panel).
int ErrorCount(const Script* s);

// True while any script is executing (unloading must wait for it).
bool AnyBusy();

}  // namespace luart
