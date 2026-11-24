-- Шаблон Lua-мода для ArkModLoader. Скопируй файл в ArkMods/ под своим именем (например
-- super_speed.lua), поменяй MOD_NAME и код — мод подхватится на ходу, перезапуск игры не нужен.
-- Справка по API: mods/README.md. Имена полей и функций игры: кнопка «DUMP SDK» в панели MODS.
MOD_NAME = "My Mod"

local RU = ark.lang() == "ru"
local function T(en, ru) return RU and ru or en end

-- Переключатель прямо в панели MODS (выбор запоминается сам).
local enabled = ark.toggle(T("MY FEATURE", "МОЯ ФУНКЦИЯ"))

-- Настройка из ArkMods/my_mod.ini: появится в окне «SETTINGS» с кнопками -/+.
local speed = ark.config("walk_speed", 1200, 100, 5000, 100)  -- по умолчанию, min, max, шаг

-- Своё окно (панель MODS -> My Mod -> OPEN >).
local win = ark.window(T("My Mod", "Мой мод"))
local info = win:label(T("Hello!", "Привет!"))
win:button(T("SAY HELLO", "ПОЗДОРОВАТЬСЯ"), function()
  ark.log("hello from my mod")           -- пишется в ArkMods/loader.log
  info:set_text(T("Hello was logged", "Записано в лог"))
end)

-- Что мы поменяли в игре: чтобы вернуть как было при выключении мода.
local original = nil

ark.on_tick(function()                   -- каждый кадр
  local pawn = ark.player()               -- твой персонаж (nil в меню)
  if not pawn then return end
  local move = pawn.CharacterMovement      -- поле по имени
  if enabled.on then
    original = original or move.MaxWalkSpeed
    move.MaxWalkSpeed = speed
  elseif original then
    move.MaxWalkSpeed = original
    original = nil
  end
end)

-- Вызывается перед выгрузкой / перезагрузкой мода.
function shutdown()
  local pawn = ark.player()
  if original and pawn then pawn.CharacterMovement.MaxWalkSpeed = original end
end
