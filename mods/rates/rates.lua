-- Rates: game multipliers (XP, taming, harvesting, damage, hunger/thirst...) for single player.
-- Each value multiplies the game's own setting; 1 = unchanged. Saved in ArkMods/rates.ini and
-- applied whenever a map is loaded. Switching the mod off puts the original values back.
MOD_NAME = "Rates"

local RU = ark.lang() == "ru"
local function T(en, ru) return RU and ru or en end

local RATES = {  -- English, Russian, ShooterGameMode field, max
  {"XP", "ОПЫТ", "XPMultiplier", 20},
  {"TAMING SPEED", "СКОРОСТЬ ПРИРУЧЕНИЯ", "TamingSpeedMultiplier", 20},
  {"HARVEST AMOUNT", "ДОБЫЧА РЕСУРСОВ", "HarvestAmountMultiplier", 20},
  {"PLAYER DAMAGE", "УРОН ИГРОКА", "PlayerDamageMultiplier", 10},
  {"PLAYER RESISTANCE", "ЗАЩИТА ИГРОКА", "PlayerResistanceMultiplier", 10},
  {"TAMED DINO DAMAGE", "УРОН ПРИРУЧЁННЫХ", "TamedDinoDamageMultiplier", 10},
  {"FOOD DRAIN", "РАСХОД ЕДЫ", "PlayerCharacterFoodDrainMultiplier", 5},
  {"WATER DRAIN", "РАСХОД ВОДЫ", "PlayerCharacterWaterDrainMultiplier", 5},
  {"STAMINA DRAIN", "РАСХОД ВЫНОСЛИВОСТИ", "PlayerCharacterStaminaDrainMultiplier", 5},
  {"HEALTH RECOVERY", "ВОССТАНОВЛЕНИЕ ЗДОРОВЬЯ", "PlayerCharacterHealthRecoveryMultiplier", 10},
}

local w = ark.window(T("Rates", "Множители"))
w:label(T("1.0 = game default. Changes apply at once.", "1.0 = как в игре. Применяется сразу."))
local controls = {}
for _, r in ipairs(RATES) do
  local key = r[3]
  controls[key] = w:number(T(r[1], r[2]), 0, r[4], 0.5, ark.config(key, 1, 0, r[4], 0.5),
                           function(v) ark.save_config(key, v) end)
end

local mode, original = nil, {}  -- game mode we changed and its own values

local function gameMode()
  local world = ark.world()
  local gm = world and world.AuthorityGameMode
  if gm and gm:is_a("/Script/ShooterGame.ShooterGameMode") then return gm end
end

local function apply()
  local gm = gameMode()
  if not gm then return end
  if mode == nil or not mode:valid() or mode ~= gm then  -- a new map: remember its values
    mode, original = gm, {}
    for _, r in ipairs(RATES) do original[r[3]] = gm[r[3]] end
  end
  for _, r in ipairs(RATES) do
    local want = original[r[3]] * controls[r[3]].value
    if math.abs(gm[r[3]] - want) > 1e-4 then gm[r[3]] = want end
  end
end

ark.every(1, apply)

function shutdown()
  if mode and mode:valid() then
    for k, v in pairs(original) do mode[k] = v end
  end
end
