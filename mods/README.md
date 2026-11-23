# Как писать моды для ArkModLoader

Мод — это `.so` под **arm64-v8a**, которая экспортирует две функции:

```cpp
#include <arkmod.h>   // loader/include/arkmod.h

extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "Мой мод"; }

extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* api) {
    api->hook_function("Actor.ReceiveBeginPlay", MyPre, nullptr, nullptr);
    api->on_tick(MyTick, nullptr);
    return 0;  // 0 = успех
}
```

## Куда класть
`/sdcard/Android/data/com.studiowildcard.wardrumstudios.ark/files/ArkMods/`.
Загрузчик подхватывает все `*.so` из этой папки в алфавитном порядке при запуске игры.
Лог загрузчика и модов лежит там же: `loader.log`.

## Правила
- В `ArkMod_Init` к объектам игры **не прикасайся**: только регистрируй хуки и тики
  (или вызови `run_on_game_thread`).
- Хуки и тики выполняются в игровом потоке, там можно вызывать функции движка.
- Пути функций пишутся так же, как в логе: `/Script/Engine.Actor.ReceiveBeginPlay`.
  Без `/` в начале путь работает как суффикс: `MainMenuUI_C.Construct`.
- Pre-хук, который вернул `0`, отменяет вызов оригинальной функции.
- Параметры функции лежат в `ctx->params`, смещение поля: `api->property_offset(ctx->func, "ИмяПараметра")`.
- Вызвать функцию игры можно через `find_function`, `params_size` и `process_event`.
  Пример — `hello_menu/hello_menu.cpp`.

## Сборка
Добавь имя мода в `foreach(mod ...)` в `loader/CMakeLists.txt` и выполни `cmake --build loader/build`.
Готовая `.so` появится в `loader/build/mods/`, а `tools/deploy.sh` сам скопирует её в игру.

Моды рассчитаны на одиночную игру и неофициальные серверы.
