# Первый мод за 5 минут

## Lua (ничего не нужно ставить)

1. Возьми шаблон [`template/my_mod.lua`](template/my_mod.lua).
2. Скопируй его в папку модов под своим именем, например `super_speed.lua`:
   `Android/data/com.studiowildcard.wardrumstudios.ark/files/ArkMods/`
   (с ПК: `adb push super_speed.lua /sdcard/Android/data/com.studiowildcard.wardrumstudios.ark/files/ArkMods/`).
3. Через пару секунд мод появится в панели **MODS**: переключатель «MY FEATURE», окно и
   строка «SETTINGS».
4. Меняй файл и сохраняй — мод перезагружается сам. Ошибки с номером строки — в `ArkMods/loader.log`.

Что можно трогать в игре: нажми **DUMP SDK** в панели MODS (лучше уже в игре, не в меню) и открой
`ArkMods/sdk/sdk.txt`. Каждое поле оттуда читается и пишется в Lua по имени (`pawn.CharacterMovement.MaxWalkSpeed`),
каждую функцию можно вызвать (`pawn:K2_TeleportTo{DestLocation = {X=0, Y=0, Z=0}, DestRotation = {}}`).

Примеры побольше: `spawner/`, `teleport/`, `rates/`, `lua_example/`.

## C++

Нужен Android NDK (r27). Шаблон: [`template/my_mod.cpp`](template/my_mod.cpp).

Самый простой способ — собрать вместе с загрузчиком: добавь `my_mod` в список
`foreach(mod hello_menu cheat_menu)` в `loader/CMakeLists.txt`, положи файл в `mods/my_mod/my_mod.cpp`
и выполни

```sh
cmake -S loader -B loader/build -G "Unix Makefiles" \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
cmake --build loader/build
```

Готовый `loader/build/mods/libmy_mod.so` положи в `ArkMods/`. Или одной командой без CMake:

```sh
$NDK/toolchains/llvm/prebuilt/<host>/bin/clang++ --target=aarch64-linux-android24 -shared -fPIC -O2 \
  -fvisibility=hidden -static-libstdc++ -I loader/include mods/template/my_mod.cpp -o libmy_mod.so
```

C++-моды тоже перезагружаются на ходу, если не используют `inline_hook`.
Полный список функций API — в [`loader/include/arkmod.h`](../loader/include/arkmod.h) и [README.md](README.md).
