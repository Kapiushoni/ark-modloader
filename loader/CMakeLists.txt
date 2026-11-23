cmake_minimum_required(VERSION 3.18)
project(arkmodloader CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(arkmodloader SHARED
    src/main.cpp src/hook.cpp src/engine.cpp src/dispatch.cpp src/mods.cpp src/log.cpp)
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
