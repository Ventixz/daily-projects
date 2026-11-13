-- Wires everything together. Shared by main.lua (LOVE), headless.lua and tests.
local Input = require("lib.input")
local Rooms = require("engine.rooms")
local Stage = require("game.stage")

local Game = {}

function Game.new(opts)
  opts = opts or {}
  local input = Input()
  input:bind("left", "left");   input:bind("a", "left")
  input:bind("right", "right"); input:bind("d", "right")
  input:bind("up", "up");       input:bind("w", "up")
  input:bind("down", "down");   input:bind("s", "down")

  local rooms = Rooms({
    input = input,
    w = opts.w or 480,
    h = opts.h or 270,
    restart_delay = opts.restart_delay,
  })
  rooms:register("Stage", Stage)
  rooms:gotoRoom("Stage")
  return rooms, input
end

return Game
