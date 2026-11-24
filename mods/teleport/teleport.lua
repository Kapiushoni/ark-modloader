-- Teleport: three saved points (kept in ArkMods/teleport.ini) and an "up" button for when you
-- are stuck. MODS panel -> Teleport -> "Teleport" OPEN >.
MOD_NAME = "Teleport"

local RU = ark.lang() == "ru"
local function T(en, ru) return RU and ru or en end

local SLOTS = 3
local w = ark.window(T("Teleport", "Телепорт"))
local status = w:label(T("Save a point, then jump back to it from anywhere.",
                         "Сохраните точку и возвращайтесь в неё откуда угодно."))

local function here()
  local pawn = ark.player()
  if not pawn or not pawn.RootComponent then return nil end
  local l = pawn.RootComponent.RelativeLocation
  return l.X, l.Y, l.Z
end

local function teleport(x, y, z)
  local pawn, pc = ark.player(), ark.controller()
  if not pawn then status:set_text(T("Not in game", "Вы не в игре")) return end
  local yaw = pc and pc.ControlRotation.Yaw or 0
  local ok = pawn:K2_TeleportTo{DestLocation = {X = x, Y = y, Z = z}, DestRotation = {Pitch = 0, Yaw = yaw, Roll = 0}}
  status:set_text(ok and string.format(T("Teleported to %.0f %.0f %.0f", "Перемещён в %.0f %.0f %.0f"), x, y, z)
                     or T("Teleport blocked here", "Сюда переместиться нельзя"))
end

local function parse(s)
  local x, y, z = tostring(s):match("^%s*(-?[%d.]+)%s*,%s*(-?[%d.]+)%s*,%s*(-?[%d.]+)%s*$")
  if x then return tonumber(x), tonumber(y), tonumber(z) end
end

for i = 1, SLOTS do
  local key = "point" .. i
  local label = w:label("")
  local function show()
    local x, y, z = parse(ark.config(key, "none"))
    label:set_text(x and string.format(T("POINT %d:  %.0f  %.0f  %.0f", "ТОЧКА %d:  %.0f  %.0f  %.0f"), i, x, y, z)
                      or string.format(T("POINT %d:  empty", "ТОЧКА %d:  пусто"), i))
  end
  show()
  w:button(T("SAVE HERE AS ", "СОХРАНИТЬ ЗДЕСЬ КАК ") .. i, function()
    local x, y, z = here()
    if not x then status:set_text(T("Not in game", "Вы не в игре")) return end
    ark.save_config(key, string.format("%.0f,%.0f,%.0f", x, y, z))
    show()
    status:set_text(string.format(T("Point %d saved", "Точка %d сохранена"), i))
  end)
  w:button(T("GO TO ", "ПЕРЕЙТИ В ") .. i, function()
    local x, y, z = parse(ark.config(key, "none"))
    if not x then status:set_text(string.format(T("Point %d is empty", "Точка %d пуста"), i)) return end
    teleport(x, y, z + 50)
  end)
end

w:button(T("UP 1000 (UNSTUCK)", "ВВЕРХ НА 1000 (ЕСЛИ ЗАСТРЯЛ)"), function()
  local x, y, z = here()
  if x then teleport(x, y, z + 1000) else status:set_text(T("Not in game", "Вы не в игре")) end
end)
