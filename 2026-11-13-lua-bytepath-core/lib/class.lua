-- Minimal single-inheritance class system.
--   local Point = Object:extend()
--   function Point:init(x, y) self.x, self.y = x, y end
--   local p = Point(1, 2)
local Object = {}
Object.__index = Object
Object.__name = "Object"

function Object:init() end

-- Create a subclass. Metamethods (keys starting with "__") are copied so
-- instances of the subclass still get __call, __tostring, etc.
function Object:extend(name)
  local cls = {}
  for k, v in pairs(self) do
    if type(k) == "string" and k:sub(1, 2) == "__" then cls[k] = v end
  end
  cls.__index = cls
  cls.__name = name or "Anonymous"
  cls.super = self
  return setmetatable(cls, self)
end

-- True when self is an instance of T or of any subclass of T.
function Object:is(T)
  local mt = getmetatable(self)
  while mt do
    if mt == T then return true end
    mt = getmetatable(mt)
  end
  return false
end

function Object:__tostring() return self.__name end

-- Calling a class constructs an instance.
function Object:__call(...)
  local obj = setmetatable({}, self)
  obj:init(...)
  return obj
end

return Object
