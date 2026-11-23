# Как писать моды для ArkModLoader (API v2)

Мод — это `.so` под **arm64-v8a**, которая экспортирует:

```cpp
#include <arkmod.h>   // loader/include/arkmod.h

extern "C" __attribute__((visibility("default"))) const char* ArkMod_Name() { return "My Mod"; }

extern "C" __attribute__((visibility("default"))) int ArkMod_Init(const ArkModAPI* api) {
    if (api->version < 2) return 1;
    api->ui_toggle("MY CHEAT", &gEnabled);          // строка ON/OFF в меню MODS
    api->on_tick(MyTick, nullptr);                  // каждый кадр
    return 0;                                       // 0 = успех
}

// Необязательно: вернуть всё как было перед выгрузкой/перезагрузкой мода.
extern "C" __attribute__((visibility("default"))) void ArkMod_Shutdown() { ... }
```

## Куда класть
`/sdcard/Android/data/com.studiowildcard.wardrumstudios.ark/files/ArkMods/`

| Файл | Что это |
|---|---|
| `libmy_mod.so` | сам мод |
| `my_mod.ini` | настройки мода, создаётся автоматически |
| `mods.ini` | какие моды включены (`1`/`0`) |
| `loader.log` | лог загрузчика и модов |

## Что происходит «на ходу» (без перезапуска игры)
- **Новый `.so`** в папке загружается через несколько секунд.
- **Изменённый `.so`**: мод выгружается (вызывается `ArkMod_Shutdown`) и загружается новая версия.
- **Удалённый `.so`** выгружается.
- **Правка `my_mod.ini`** подхватывается в течение секунды.
- **Меню MODS** в игре: строка с названием мода включает или выключает его, под ней идут переключатели самого мода.

При выгрузке загрузчик сам удаляет всё, что мод зарегистрировал через API: хуки функций, тики, задачи, отрисовку, касания и переключатели.
**Исключение:** мод, который вызвал `inline_hook`, на ходу не выгружается. Изменения для него применятся после перезапуска игры, а в меню будет пометка.

## Настройки
```cpp
float speed = api->config_float("fly_speed", 1200.f);   // в файле появится "fly_speed = 1200"
const char* s = api->config_string("text", "hello");     // указатель живёт до следующего чтения: копируй
```
Если ключа нет в файле, он дописывается со значением по умолчанию. Так пользователь видит, что можно настроить.

## Интерфейс
- `ui_toggle(label, &int)`: переключатель в меню MODS. Мод сам читает значение в тике.
- `on_draw(fn, user)`: рисовать поверх HUD каждый кадр через `draw_rect` и `draw_text` (только ASCII, координаты в пикселях экрана).
- `on_touch(fn, user)`: касания. Верни `1` на `phase == 0` (нажатие), и это касание (движение и отпускание) достанется тебе, а не игре.

## Правила
- `ArkMod_Init`, `ArkMod_Shutdown`, хуки, тики, отрисовка и касания выполняются в игровом потоке, там можно вызывать функции движка.
- Пути функций пишутся так же, как в логе: `/Script/Engine.Actor.ReceiveBeginPlay`. Без `/` в начале путь работает как суффикс: `MainMenuUI_C.Construct`.
- Pre-хук, который вернул `0`, отменяет вызов оригинальной функции.
- Примеры: `hello_menu` (настройки и восстановление при выгрузке), `cheat_menu` (переключатели, отрисовка, касания).

## Сборка
Добавь имя мода в `foreach(mod ...)` в `loader/CMakeLists.txt` и выполни `cmake --build loader/build`.
Готовая `.so` появится в `loader/build/mods/`, а `tools/deploy.sh` скопирует её в игру. Чтобы обновить один мод без перезапуска игры, достаточно `adb push` нового файла в папку `ArkMods`.

Моды рассчитаны на одиночную игру и неофициальные серверы.

---

# Lua-моды (без компилятора)

Lua-мод — это обычный текстовый файл `имя.lua` в той же папке `ArkMods`. Собирать ничего не нужно.
Сохранил файл, и мод через пару секунд перезагрузился. Ошибки пишутся в `loader.log` с номером строки, игра от них не падает.
Пример: `mods/lua_example/lua_example.lua`.

```lua
MOD_NAME = "My Lua Mod"                       -- название в меню MODS

local fast = ark.toggle("FAST RUN")           -- переключатель в меню (fast.on = true/false)
local mul  = ark.config("multiplier", 2)      -- настройка из my_lua_mod.ini

ark.on_tick(function()                        -- каждый кадр
  local pawn = ark.player()                   -- твой персонаж (или nil)
  if pawn and fast.on then
    pawn.CharacterMovement.MaxWalkSpeed = 600 * mul   -- любое поле по имени
  end
end)

function shutdown()                           -- перед выгрузкой/перезагрузкой: верни как было
end
```

## Функции `ark`
| Функция | Что делает |
|---|---|
| `ark.log(...)` / `print(...)` | запись в лог |
| `ark.config(key, default)` | настройка из `<мод>.ini`; тип как у `default` (число / строка / true-false) |
| `ark.toggle(label [, default])` | переключатель в меню MODS, значение в `t.on` |
| `ark.on_tick(fn)` | `fn()` каждый кадр |
| `ark.on_draw(fn)` | `fn(w, h)` каждый кадр, внутри можно `ark.draw_rect(x,y,w,h, r,g,b,a)` и `ark.draw_text(text, x,y, scale, r,g,b)` |
| `ark.on_touch(fn)` | `fn(phase, finger, x, y)`; верни `true` на `phase == 0`, чтобы забрать касание у игры |
| `ark.hook(path, pre, post)` | перехват функции игры: `pre(self, params)` / `post(self, params)`; `pre` вернул `false` — оригинал не вызывается |
| `ark.player()`, `ark.controller()` | твой персонаж и контроллер |
| `ark.world()`, `ark.world_name()` | текущий мир |
| `ark.find(path)` | объект или класс по пути, например `"/Script/Engine.Actor"` |
| `ark.objects(class [, limit])` | все живые объекты класса (класс — объект или путь) |
| `ark.time()` | секунды (монотонно) |

## Объекты игры
- `obj.Свойство` и `obj.Свойство = значение` читают и пишут поле по имени: числа, bool, объекты, enum, `FName`, `FText`, структуры.
- Структуры читаются как ссылка (`loc.X`, `move.Velocity.Z = 0`), а присваиваются таблицей (`obj.Color = {R=1, G=0, B=0, A=1}`).
- Статические массивы: `obj:get("CurrentStatusValues", 0)` / `obj:set("CurrentStatusValues", 100, 0)`. Индексы с 0.
- `obj:Функция{Param = 1}` или `obj:Функция(1, 2)` вызывает функцию игры. Возвращает `ReturnValue` и таблицу всех параметров.
- `obj:valid()`, `obj:name()`, `obj:path()`, `obj:class()`, `obj:class_name()`, `obj:is_a(classOrPath)`.
- Обращение к уничтоженному объекту выдаёт ошибку Lua, а не падение игры.
