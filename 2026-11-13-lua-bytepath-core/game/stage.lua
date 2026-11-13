-- The gameplay room: one Area holding the player and its projectiles.
-- When the player dies the stage asks the room manager for a fresh Stage
-- one second later, which is the whole restart logic.
local Object = require("lib.class")
local Timer = require("lib.timer")
local Area = require("engine.area")
local Player = require("game.player")
local Projectile = require("game.projectile")

local Stage = Object:extend("Stage")

function Stage:init(rooms)
  self.rooms = rooms
  local ctx = rooms.ctx
  self.w, self.h = ctx.w or 480, ctx.h or 270
  ctx.runs = (ctx.runs or 0) + 1
  self.run = ctx.runs
  self.timer = Timer()
  self.area = Area(self)
  self.player = self.area:addGameObject(Player, self.w / 2, self.h / 2, { input = ctx.input })
  self.player.on_death = function()
    self.timer:after(ctx.restart_delay or 1, function() rooms:request("Stage") end, "restart")
  end
end

function Stage:update(dt)
  self.timer:update(dt)
  self.area:update(dt)
end

function Stage:draw(gfx) self.area:draw(gfx) end

function Stage:destroy()
  self.area:destroy()
  self.timer:clear()
end

function Stage:projectileCount()
  return #self.area:getGameObjects(function(o) return o:is(Projectile) end)
end

-- 8-way arrow for the ship heading (screen y points down).
local ARROWS = { ">", "\\", "v", "/", "<", "\\", "^", "/" }
local function arrow(r)
  local a = r % (2 * math.pi)
  local sector = math.floor((a + math.pi / 8) / (math.pi / 4)) % 8
  return ARROWS[sector + 1]
end

-- Text rendering for headless runs: the play area scaled to cols x rows.
function Stage:renderAscii(cols, rows)
  local grid = {}
  for y = 1, rows do
    grid[y] = {}
    for x = 1, cols do grid[y][x] = " " end
  end
  local function plot(obj, ch)
    local cx = math.floor(obj.x / self.w * cols) + 1
    local cy = math.floor(obj.y / self.h * rows) + 1
    if cx >= 1 and cx <= cols and cy >= 1 and cy <= rows then grid[cy][cx] = ch end
  end
  for _, obj in ipairs(self.area:drawOrder()) do
    if obj:is(Player) then plot(obj, arrow(obj.r)) else plot(obj, "*") end
  end
  local border = "+" .. string.rep("-", cols) .. "+"
  local lines = { border }
  for y = 1, rows do lines[#lines + 1] = "|" .. table.concat(grid[y]) .. "|" end
  lines[#lines + 1] = border
  return table.concat(lines, "\n")
end

return Stage
