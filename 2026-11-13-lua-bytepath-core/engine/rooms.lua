-- Room manager. A room is a whole screen of the game (menu, stage, ...).
-- By default gotoRoom() builds a fresh room and destroys the old one, which
-- gives a clean restart for free. A room class with `persistent = true` is
-- built once and reused, keeping its state between visits.
local Object = require("lib.class")

local Rooms = Object:extend("Rooms")

-- LuaJIT (used by LOVE) is Lua 5.1: no table.pack, unpack is global.
local unpack = table.unpack or unpack
local function pack(...) return { n = select("#", ...), ... } end

-- ctx is shared state every room can reach (input, screen size, ...).
function Rooms:init(ctx)
  self.ctx = ctx or {}
  self.classes = {}
  self.cache = {}       -- name -> instance, persistent rooms only
  self.current = nil
  self.current_name = nil
  self.pending = nil
end

function Rooms:register(name, cls) self.classes[name] = cls end

function Rooms:_leave()
  local cur = self.current
  if not cur then return end
  if cur.leave then cur:leave() end
  if not self.classes[self.current_name].persistent and cur.destroy then
    cur:destroy()
  end
end

-- Switch immediately. Avoid calling this from inside the current room's
-- update; use request() so the switch happens between frames.
function Rooms:gotoRoom(name, ...)
  local cls = assert(self.classes[name], "unknown room: " .. tostring(name))
  self:_leave()
  local room
  if cls.persistent then
    room = self.cache[name]
    if not room then
      room = cls(self, ...)
      self.cache[name] = room
    end
  else
    room = cls(self, ...)
  end
  self.current, self.current_name = room, name
  if room.enter then room:enter(...) end
  return room
end

function Rooms:request(name, ...)
  self.pending = { name = name, args = pack(...) }
end

function Rooms:update(dt)
  if self.current and self.current.update then self.current:update(dt) end
  if self.pending then
    local p = self.pending
    self.pending = nil
    self:gotoRoom(p.name, unpack(p.args, 1, p.args.n))
  end
end

function Rooms:draw(gfx)
  if self.current and self.current.draw then self.current:draw(gfx) end
end

return Rooms
