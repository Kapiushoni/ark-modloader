cmake_minimum_required(VERSION 3.18)
project(arkmodloader C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(arkmodloader SHARED
    src/main.cpp src/hook.cpp src/engine.cpp src/dispatch.cpp src/mods.cpp src/log.cpp src/ui.cpp src/lua_runtime.cpp)

# Lua 5.4.7 (vendored, MIT): built as C into the loader.
file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/lua/*.c)
add_library(lua_static STATIC ${LUA_SOURCES})
target_compile_options(lua_static PRIVATE -O2 -fvisibility=hidden -fPIC -w)
target_compile_definitions(lua_static PRIVATE LUA_USE_LINUX)
target_include_directories(arkmodloader PRIVATE lua)
target_link_libraries(arkmodloader PRIVATE lua_static)
target_include_directories(arkmodloader PRIVATE include src)
target_compile_options(arkmodloader PRIVATE -fvisibility=hidden -O2 -Wall)
target_link_libraries(arkmodloader PRIVATE log dl)
target_link_options(arkmodloader PRIVATE -static-libstdc++ -Wl,--gc-sections -s)

# Example mods
foreach(mod hello_menu cheat_menu)
    add_library(${mod} SHARED ../mods/${mod}/${mod}.cpp)
    target_include_directories(${mod} PRIVATE include)
    target_compile_options(${mod} PRIVATE -fvisibility=hidden -O2 -Wall)
    target_link_options(${mod} PRIVATE -static-libstdc++ -s)
    set_target_properties(${mod} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/mods)
endforeach()
