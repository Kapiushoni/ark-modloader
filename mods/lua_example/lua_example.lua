-- Example Lua mod for ArkModLoader. Drop it into ArkMods/ — no compiling needed.
-- Edit and save the file while the game runs: it is reloaded automatically.
MOD_NAME = "Lua Example"

local jump = ark.toggle("SUPER JUMP")          -- row in the MODS panel
local showPos = ark.toggle("SHOW POSITION", true)

local origJump = nil
local lastWorld = nil
local reportedTime = false

ark.on_tick(function()
  local world = ark.world_name()
  if world ~= lastWorld then
    ark.log("world ->", world)
    lastWorld = world
    reportedTime = false
  end

  local pawn = ark.player()
  if not pawn then return end
  local move = pawn.CharacterMovement
  if not move then return end

  -- Reflection: read/write any property by name.
  if jump.on then
    origJump = origJump or move.JumpZVelocity
    move.JumpZVelocity = origJump * ark.config("jump_multiplier", 3)
  elseif origJump then
    move.JumpZVelocity = origJump
    origJump = nil
  end

  -- Calling a game function (method syntax on any UObject).
  if not reportedTime then
    reportedTime = true
    local pc = ark.controller()
    local ok, t = pcall(function() return pc:GetTimeOfDayString() end)
    ark.log("time of day:", ok and t or ("call failed: " .. tostring(t)))
  end
end)

ark.on_draw(function(w, h)
  if not showPos.on then return end
  local pawn = ark.player()
  if not pawn or not pawn.RootComponent then return end
  local loc = pawn.RootComponent.RelativeLocation   -- struct: fields by name
  local s = h / 900
  ark.draw_rect(20 * s, 105 * s, 470 * s, 48 * s, 0, 0, 0, 0.5)
  ark.draw_text(string.format("X %.0f   Y %.0f   Z %.0f", loc.X, loc.Y, loc.Z), 34 * s, 112 * s, 1.6 * s)
end)

-- Called before the mod is unloaded or reloaded: put things back.
function shutdown()
  local pawn = ark.player()
  if origJump and pawn and pawn.CharacterMovement then
    pawn.CharacterMovement.JumpZVelocity = origJump
  end
end
