-- Base class for everything that lives inside an Area.
local Object = require("lib.class")
local Timer = require("lib.timer")

local GameObject = Object:extend("GameObject")

function GameObject:init(area, x, y, opts)
  if opts then for k, v in pairs(opts) do self[k] = v end end
  self.area = area
  self.x, self.y = x or 0, y or 0
  self.id = area and area:nextId() or 0
  self.depth = self.depth or 50
  self.dead = false
  self.timer = Timer()
end

function GameObject:update(dt)
  if self.timer then self.timer:update(dt) end
end

function GameObject:draw(_gfx) end

-- Called by the Area when the object is removed. Break references so the
-- garbage collector can reclaim the object and nothing calls back into it.
function GameObject:destroy()
  if self.timer then self.timer:clear() end
  self.timer = nil
  self.area = nil
end

return GameObject
