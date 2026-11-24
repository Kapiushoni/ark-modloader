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
- **Меню MODS** (пункт под EXIT в главном меню и кнопка сверху в игре): строка с названием мода включает или выключает его, под ней идут переключатели самого мода.

При выгрузке загрузчик сам удаляет всё, что мод зарегистрировал через API: хуки функций, тики, задачи, отрисовку, касания и переключатели.
**Исключение:** мод, который вызвал `inline_hook`, на ходу не выгружается. Изменения для него применятся после перезапуска игры, а в меню будет пометка.

## Настройки
```cpp
float speed = api->config_float("fly_speed", 1200.f);   // в файле появится "fly_speed = 1200"
const char* s = api->config_string("text", "hello");     // указатель живёт до следующего чтения: копируй
```
Если ключа нет в файле, он дописывается со значением по умолчанию. Так пользователь видит, что можно настроить.

## Интерфейс
- `ui_toggle(label, &int)`: переключатель в меню MODS. Мод сам читает значение в тике. Загрузчик запоминает выбор пользователя (`toggle.<label> = 0/1` в `<мод>.ini`) и восстанавливает его при следующей загрузке мода.
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
| `ark.config(key, default [, min, max, step])` | настройка из `<мод>.ini`; тип как у `default` (число / строка / true-false). Для чисел можно задать диапазон и шаг для окна SETTINGS |
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
| `ark.window(title)` | своё окно в стиле игры (см. ниже) |
| `ark.delay(sec, fn)`, `ark.every(sec, fn)` → id, `ark.cancel(id)` | таймеры (по кадрам игры: пока игра стоит, например при загрузке, они ждут) |
| `ark.on_new_object(class, fn)` | `fn(obj)` для каждого нового объекта этого класса (или подкласса), в следующем кадре после создания |
| `ark.load(path)` | найти объект или загрузить его из пакетов игры (`"/Game/.../Dodo_Character_BP.Dodo_Character_BP_C"`) |
| `ark.spawn(class, x, y, z [, yaw])` | заспавнить актора (класс — объект или путь), вернёт его или `nil` |
| `ark.dump_sdk([filter])` | дамп SDK в `ArkMods/sdk/` (см. ниже) |
| `ark.save_config(key, value)` | записать настройку в `<мод>.ini` (остальные строки сохраняются) |
| `ark.lang()` | язык интерфейса загрузчика: `"en"` или `"ru"` — чтобы сделать мод на двух языках |

## Объекты игры
- `obj.Свойство` и `obj.Свойство = значение` читают и пишут поле по имени: числа, bool, объекты, enum, `FName`, `FText`, структуры.
- Структуры читаются как ссылка (`loc.X`, `move.Velocity.Z = 0`), а присваиваются таблицей (`obj.Color = {R=1, G=0, B=0, A=1}`).
- Статические массивы: `obj:get("CurrentStatusValues", 0)` / `obj:set("CurrentStatusValues", 100, 0)`. Индексы с 0.
- `TArray`: `obj.Arr` — копия-таблица (индексы с 1, `#obj.Arr` — длина); `obj:get("Arr", i)` / `obj:set("Arr", v, i)` — один элемент на месте (индексы с 0);
  `obj.Arr = {a, b, c}` — заменить массив целиком (для чисел, bool, enum, объектов, `FName` и структур из них).
- `obj:Функция{Param = 1}` или `obj:Функция(1, 2)` вызывает функцию игры. Возвращает `ReturnValue` и таблицу всех параметров.
- `obj:valid()`, `obj:name()`, `obj:path()`, `obj:class()`, `obj:class_name()`, `obj:is_a(classOrPath)`.
- Обращение к уничтоженному объекту выдаёт ошибку Lua, а не падение игры.

## Окна (API v3)

Мод может сделать своё окно из виджетов самой игры, в том же стиле, что и панель MODS.
Окно появляется в панели MODS под модом строкой «OPEN >», его можно открыть и из кода.
Одновременно открыто одно окно (или панель). При выгрузке мода его окна исчезают сами.

```lua
local w = ark.window("Teleport")
local info = w:label("Saved point: none")          -- надпись
w:button("SAVE POINT", function() ... end)         -- кнопка
local god = w:check("GOD MODE", false, function(on) ... end)        -- ON/OFF, god.on
local speed = w:number("SPEED", 1, 10, 0.5, 2, function(v) ... end) -- min, max, step, value; speed.value
info:set_text("Saved point: 100, 200")
w:show()  w:hide()  w:toggle()  w:visible()  w:set_title("...")
```

На C++ то же самое через `api->ui_window`, `ui_label`, `ui_button`, `ui_check`, `ui_number`,
`ui_set_text`, `ui_show`, `ui_visible` (см. `arkmod.h`). Значения переключателей и чисел хранит мод
(`int*` / `float*`): окно показывает их изменения само.

## Настройки в панели (SETTINGS)

У каждого мода, у которого в `<мод>.ini` есть настройки, в панели MODS появляется строка
«SETTINGS >». Окно настроек загрузчик собирает сам из ini: числа меняются кнопками −/+,
`true/false`, `on/off`, `yes/no` показываются как ON/OFF, текстовые значения — только для чтения.
Изменения сразу записываются в ini, а моды и так перечитывают настройки на лету.

Шаг −/+ загрузчик подбирает по значению. Чтобы задать точно:
```cpp
api->config_range("fly_speed", 100, 5000, 100);   // C++, вызывать в ArkMod_Init
```
```lua
local mul = ark.config("jump_multiplier", 3, 1, 10, 0.5)   -- Lua: default, min, max, step
```

## Дамп SDK: какие поля и функции есть в игре

Панель MODS → внизу **DUMP SDK** (или `ark.dump_sdk()` в Lua). Загрузчик записывает в
`ArkMods/sdk/sdk.txt` все классы, структуры и enum'ы, которые сейчас загружены, с типами,
смещениями и размерами полей и сигнатурами функций:

```
class PrimalCharacterStatusComponent : ActorComponent {  // /Script/ShooterGame.PrimalCharacterStatusComponent, size 0x11e0
    0x0960  float                                    CurrentStatusValues[12];  // size 0x30
    fn void SetMovementMode(enum EMovementMode (uint8) NewMovementMode, uint8 NewCustomMode);  // native callable params 0x2
```
Имена — ровно те, что принимают Lua (`obj.CurrentStatusValues`, `obj:SetMovementMode(5, 0)`) и C API
(`property_offset`, `find_function`). Blueprint-классы карты (`*_C`) загружаются вместе с миром:
для полного дампа запускай его в игре, а не в главном меню. `ark.dump_sdk("PrimalDino")` пишет только
типы, в пути которых есть эта строка (`sdk_PrimalDino.txt`). Пока идёт дамп (несколько секунд),
игра стоит.

## Язык

Панель MODS переведена на русский. Язык выбирается в `ArkMods/loader.ini`:
`language = auto` (язык устройства), `en` или `ru`. Моды могут подстраиваться под него через
`ark.lang()`: см. `mods/spawner/spawner.lua` — `local function T(en, ru) ... end`.
Шрифт игры поддерживает кириллицу.
