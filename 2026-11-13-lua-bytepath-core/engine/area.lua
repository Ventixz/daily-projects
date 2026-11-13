-- An Area owns a list of GameObjects: it updates them, removes the dead
-- ones and answers spatial queries. Rooms hold one or more Areas.
local Object = require("lib.class")

local Area = Object:extend("Area")

function Area:init(room)
  self.room = room
  self.game_objects = {}
  self.id_counter = 0
end

function Area:nextId()
  self.id_counter = self.id_counter + 1
  return self.id_counter
end

function Area:addGameObject(cls, x, y, opts)
  local obj = cls(self, x, y, opts)
  self.game_objects[#self.game_objects + 1] = obj
  return obj
end

function Area:update(dt)
  -- Walk backwards so table.remove doesn't skip the next element. Objects
  -- spawned during this loop are appended past `n` and first update next frame.
  local n = #self.game_objects
  for i = n, 1, -1 do
    local obj = self.game_objects[i]
    if not obj.dead then obj:update(dt) end
    if obj.dead then
      obj:destroy()
      table.remove(self.game_objects, i)
    end
  end
end

-- Draw lower depth first; ties broken by creation order for stability.
function Area:drawOrder()
  local sorted = {}
  for i, obj in ipairs(self.game_objects) do sorted[i] = obj end
  table.sort(sorted, function(a, b)
    if a.depth == b.depth then return a.id < b.id end
    return a.depth < b.depth
  end)
  return sorted
end

function Area:draw(gfx)
  for _, obj in ipairs(self:drawOrder()) do obj:draw(gfx) end
end

local function matchesClass(obj, classes)
  if not classes then return true end
  for _, cls in ipairs(classes) do
    if obj:is(cls) then return true end
  end
  return false
end

function Area:getGameObjects(filter)
  local out = {}
  for _, obj in ipairs(self.game_objects) do
    if not filter or filter(obj) then out[#out + 1] = obj end
  end
  return out
end

function Area:queryCircleArea(x, y, r, classes)
  local r2 = r * r
  return self:getGameObjects(function(o)
    local dx, dy = o.x - x, o.y - y
    return not o.dead and matchesClass(o, classes) and dx * dx + dy * dy <= r2
  end)
end

function Area:getClosestObject(x, y, r, classes)
  local best, best_d2 = nil, math.huge
  for _, o in ipairs(self:queryCircleArea(x, y, r, classes)) do
    local dx, dy = o.x - x, o.y - y
    local d2 = dx * dx + dy * dy
    if d2 < best_d2 then best, best_d2 = o, d2 end
  end
  return best
end

function Area:destroy()
  for _, obj in ipairs(self.game_objects) do obj:destroy() end
  self.game_objects = {}
end

return Area
