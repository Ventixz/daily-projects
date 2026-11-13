-- Run the game with no window: scripted key presses, jittery frame times
-- fed through the fixed-timestep loop, and ASCII snapshots of the stage.
--   lua headless.lua            full demo
--   lua headless.lua --quiet    summary line only
package.path = "./?.lua;" .. package.path
local Game = require("game.init")
local FixedLoop = require("engine.loop")

local Headless = {}

-- frame -> list of {"press"|"release", key}
local DEFAULT_SCRIPT = {
  [20]  = { { "press", "right" } },                        -- swing right
  [50]  = { { "release", "right" } },
  [60]  = { { "press", "up" } },                           -- boost
  [80]  = { { "release", "up" }, { "press", "right" } },   -- circle back
  [125] = { { "release", "right" }, { "press", "down" } }, -- brake
  [170] = { { "release", "down" }, { "press", "left" } },  -- loop left
  [230] = { { "release", "left" } },                       -- fly off and die
}

-- Real frame times wobble; cycle through a fixed pattern so runs repeat.
local JITTER = { 1 / 60, 1 / 50, 1 / 75, 1 / 60, 1 / 40, 1 / 90 }

function Headless.run(opts)
  opts = opts or {}
  local frames = opts.frames or 480
  local script = opts.script or DEFAULT_SCRIPT
  local rooms, input = Game.new({ restart_delay = opts.restart_delay })
  local loop = FixedLoop(1 / 60)
  local snapshots = {}

  for frame = 1, frames do
    for _, ev in ipairs(script[frame] or {}) do
      if ev[1] == "press" then input:keypressed(ev[2]) else input:keyreleased(ev[2]) end
    end
    loop:advance(JITTER[(frame - 1) % #JITTER + 1], function(dt) rooms:update(dt) end)
    input:endFrame()
    if opts.snapshot_every and frame % opts.snapshot_every == 0 then
      snapshots[#snapshots + 1] = { frame = frame, text = rooms.current:renderAscii(48, 12) }
    end
  end

  local stage = rooms.current
  local p = stage.player
  return {
    ticks = loop.ticks,
    run = stage.run,
    x = p.x, y = p.y, r = p.r, v = p.v,
    shots = p.shots,
    projectiles = stage:projectileCount(),
    snapshots = snapshots,
  }
end

if not pcall(debug.getlocal, 4, 1) then   -- executed as a script, not required
  local quiet = arg and arg[1] == "--quiet"
  local res = Headless.run({ snapshot_every = quiet and nil or 60 })
  if not quiet then
    for _, s in ipairs(res.snapshots) do
      print(("frame %d"):format(s.frame))
      print(s.text)
    end
  end
  print(("ticks=%d run=%d pos=(%.1f, %.1f) heading=%.2f speed=%.1f shots=%d live_projectiles=%d")
    :format(res.ticks, res.run, res.x, res.y, res.r, res.v, res.shots, res.projectiles))
end

return Headless
