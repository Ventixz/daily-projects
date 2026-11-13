local GameObject = require("engine.gameobject")

local Projectile = GameObject:extend("Projectile")

function Projectile:init(area, x, y, opts)
  Projectile.super.init(self, area, x, y, opts)
  self.r = self.r or 0
  self.v = self.v or 200
  self.s = self.s or 2.5
  self.depth = 40
end

function Projectile:update(dt)
  Projectile.super.update(self, dt)
  self.x = self.x + self.v * math.cos(self.r) * dt
  self.y = self.y + self.v * math.sin(self.r) * dt
  local room = self.area.room
  if self.x < 0 or self.y < 0 or self.x > room.w or self.y > room.h then
    self.dead = true
  end
end

function Projectile:draw(gfx)
  gfx.circle("fill", self.x, self.y, self.s)
end

return Projectile
