-- The ship: always moving forward, steered by rotating. Up boosts, down
-- brakes. Fires automatically on a fixed interval. Leaving the play area
-- kills it.
local GameObject = require("engine.gameobject")
local Projectile = require("game.projectile")

local Player = GameObject:extend("Player")

function Player:init(area, x, y, opts)
  Player.super.init(self, area, x, y, opts)
  self.w = 12
  self.r = -math.pi / 2          -- facing up (y grows downwards)
  self.rv = 1.66 * math.pi       -- turn speed, radians per second
  self.v = 0
  self.base_max_v = 100
  self.max_v = self.base_max_v
  self.a = 100                   -- acceleration
  self.boosting = false
  self.shots = 0
  self.depth = 60
  self.timer:every(self.shoot_interval or 0.24, function() self:shoot() end, nil, "shoot")
end

function Player:update(dt)
  Player.super.update(self, dt)
  if self.dead then return end   -- a timer callback may have killed us

  local input = self.input
  if input:down("left") then self.r = self.r - self.rv * dt end
  if input:down("right") then self.r = self.r + self.rv * dt end

  self.boosting = false
  self.max_v = self.base_max_v
  if input:down("up") then
    self.boosting = true
    self.max_v = 1.5 * self.base_max_v
  elseif input:down("down") then
    self.max_v = 0.5 * self.base_max_v
  end

  -- Accelerate towards max_v; braking clamps the speed down instantly.
  self.v = math.min(self.v + self.a * dt, self.max_v)
  self.x = self.x + self.v * math.cos(self.r) * dt
  self.y = self.y + self.v * math.sin(self.r) * dt

  local room = self.area.room
  if self.x < 0 or self.y < 0 or self.x > room.w or self.y > room.h then
    self:die()
  end
end

function Player:shoot()
  local d = 1.2 * self.w         -- spawn just ahead of the nose
  self.area:addGameObject(Projectile,
    self.x + d * math.cos(self.r), self.y + d * math.sin(self.r),
    { r = self.r })
  self.shots = self.shots + 1
end

function Player:die()
  if self.dead then return end
  self.dead = true
  if self.on_death then self.on_death(self) end
end

function Player:draw(gfx)
  gfx.circle("line", self.x, self.y, self.w)
  gfx.line(self.x, self.y,
    self.x + 2 * self.w * math.cos(self.r), self.y + 2 * self.w * math.sin(self.r))
end

return Player
