-- Spawner: dinos (with a level) next to you and resources into your inventory.
-- MODS panel -> Spawner -> "Spawner" OPEN >.  Single player / unofficial servers only.
MOD_NAME = "Spawner"

local RU = ark.lang() == "ru"
local function T(en, ru) return RU and ru or en end

local DINOS = {  -- English, Russian, path under /Game/PrimalEarth/Dinos/
  {"DODO", "ДОДО", "Dodo/Dodo_Character_BP"},
  {"RAPTOR", "РАПТОР", "Raptor/Raptor_Character_BP"},
  {"TRIKE", "ТРИЦЕРАТОПС", "Trike/Trike_Character_BP"},
  {"STEGO", "СТЕГОЗАВР", "Stego/Stego_Character_BP"},
  {"PARASAUR", "ПАРАЗАВР", "Para/Para_Character_BP"},
  {"CARNO", "КАРНОТАВР", "Carno/Carno_Character_BP"},
  {"REX", "РЕКС", "Rex/Rex_Character_BP"},
  {"BRONTO", "БРОНТОЗАВР", "Sauropod/Sauropod_Character_BP"},
  {"PTERANODON", "ПТЕРАНОДОН", "Ptero/Ptero_Character_BP"},
  {"ARGENTAVIS", "АРГЕНТАВИС", "Argentavis/Argent_Character_BP"},
  {"DIRE BEAR", "ЛЮТЫЙ МЕДВЕДЬ", "Direbear/Direbear_Character_BP"},
  {"GALLIMIMUS", "ГАЛЛИМИМ", "Gallimimus/Galli_Character_BP"},
}
local ITEMS = {  -- English, Russian, PrimalItemResource_<name>
  {"WOOD", "ДЕРЕВО", "Wood"}, {"STONE", "КАМЕНЬ", "Stone"}, {"FIBER", "ВОЛОКНО", "Fibers"},
  {"THATCH", "СОЛОМА", "Thatch"}, {"FLINT", "КРЕМЕНЬ", "Flint"}, {"HIDE", "ШКУРА", "Hide"},
  {"METAL INGOT", "МЕТАЛЛИЧЕСКИЙ СЛИТОК", "MetalIngot"}, {"CRYSTAL", "КРИСТАЛЛ", "Crystal"},
  {"OBSIDIAN", "ОБСИДИАН", "Obsidian"}, {"OIL", "НЕФТЬ", "Oil"}, {"POLYMER", "ПОЛИМЕР", "Polymer"},
  {"SPARKPOWDER", "ИСКРОВЫЙ ПОРОШОК", "Sparkpowder"}, {"CEMENTING PASTE", "ЦЕМЕНТИРУЮЩАЯ ПАСТА", "ChitinPaste"},
}

local function dinoPath(p) return "/Game/PrimalEarth/Dinos/" .. p .. "." .. p:match("([^/]+)$") .. "_C" end
local function itemPath(n)
  return "/Game/PrimalEarth/CoreBlueprints/Resources/PrimalItemResource_" .. n .. ".PrimalItemResource_" .. n .. "_C"
end

local w = ark.window(T("Spawner", "Спавнер"))
local status = w:label(T("Dinos appear in front of you.", "Динозавры появляются перед вами."))
local level = w:number(T("DINO LEVEL", "УРОВЕНЬ ДИНО"), 1, 150, 5, ark.config("dino_level", 30, 1, 150, 5),
                       function(v) ark.save_config("dino_level", v) end)
local qty  -- QUANTITY control (created further down, under RESOURCES)

-- A point `dist` units in front of the player, on the camera's yaw.
local function inFront(dist)
  local pawn, pc = ark.player(), ark.controller()
  if not pawn or not pc then return nil end
  local loc = pawn.RootComponent.RelativeLocation
  local yaw = pc.ControlRotation.Yaw
  local r = math.rad(yaw)
  return loc.X + math.cos(r) * dist, loc.Y + math.sin(r) * dist, loc.Z + 50, yaw
end

local function spawnDino(label, path)
  local x, y, z, yaw = inFront(700)
  if not x then status:set_text(T("Not in game", "Вы не в игре")) return end
  local cls = ark.load(dinoPath(path))
  if not cls then status:set_text(label .. T(": not in this version of the game", ": нет в этой версии игры")) return end
  -- PrimalDinoCharacter.SpawnDino (static) sets the dino up like a wild one (its level overrides are
  -- ignored on mobile, so the level is applied afterwards with the status component's SetBaseLevel).
  local cdo = ark.find("/Script/ShooterGame.Default__PrimalDinoCharacter")
  local ok, dino = pcall(function()
    return cdo:SpawnDino{World = ark.world(), DinoClass = cls,
                         SpawnLoc = {X = x, Y = y, Z = z}, SpawnRot = {Pitch = 0, Yaw = yaw + 180, Roll = 0},
                         LevelMultiplier = 1, NPCAIRangeMultiplier = 1}
  end)
  if not ok or not dino then dino = ark.spawn(cls, x, y, z, yaw + 180) end  -- plain spawn as a fallback
  if not dino then status:set_text(label .. T(": spawn failed", ": не удалось заспавнить")) return end
  local st = dino.MyCharacterStatusComponent
  if st then st:SetBaseLevel(math.floor(level.value), false) end
  status:set_text(label .. T(" spawned, level ", ", уровень ") .. (st and st:GetCharacterLevel() or "?"))
end

local function giveItem(label, name)
  local pawn = ark.player()
  local inv = pawn and pawn.MyInventoryComponent
  if not inv then status:set_text(T("Not in game", "Вы не в игре")) return end
  local cls = ark.load(itemPath(name))
  if not cls then status:set_text(label .. T(": not found", ": не найдено")) return end
  local cdo = ark.find("/Script/ShooterGame.Default__PrimalItem")
  local n = math.floor(qty.value)
  local item = cdo:AddNewItem{ItemArchetype = cls, GiveToInventory = inv, bForceNoBlueprint = true,
                              quantityOverride = n, ShowHUDNotification = true}
  status:set_text(item and (n .. " x " .. label) or (label .. T(": failed", ": не получилось")))
end

w:label(T("DINOS", "ДИНОЗАВРЫ"))
for _, d in ipairs(DINOS) do
  local label = T(d[1], d[2])
  w:button(label, function() spawnDino(label, d[3]) end)
end

w:label(T("RESOURCES", "РЕСУРСЫ"))
qty = w:number(T("QUANTITY", "КОЛИЧЕСТВО"), 1, 1000, 10, ark.config("quantity", 100, 1, 1000, 10),
               function(v) ark.save_config("quantity", v) end)
for _, it in ipairs(ITEMS) do
  local label = T(it[1], it[2])
  w:button(label, function() giveItem(label, it[3]) end)
end
