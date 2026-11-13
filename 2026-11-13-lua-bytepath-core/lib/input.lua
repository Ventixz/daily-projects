-- Action-based input. Keys are bound to named actions, and game code asks
-- about actions ("left", "shoot"), never about raw keys.
--   pressed(a)  true only on the frame the key went down
--   released(a) true only on the frame it came up
--   down(a)     true while held
-- Call endFrame() once per frame, after game logic has read the state.
local Object = require("lib.class")

local Input = Object:extend("Input")

function Input:init()
  self.bindings = {}     -- key -> { action = true, ... }
  self.held = {}         -- key -> true
  self.pressed_keys = {}
  self.released_keys = {}
end

function Input:bind(key, action)
  self.bindings[key] = self.bindings[key] or {}
  self.bindings[key][action] = true
end

function Input:unbindAll() self.bindings = {} end

function Input:keypressed(key)
  if not self.held[key] then self.pressed_keys[key] = true end
  self.held[key] = true
end

function Input:keyreleased(key)
  if self.held[key] then self.released_keys[key] = true end
  self.held[key] = nil
end

local function any(self, set, action)
  for key in pairs(set) do
    local acts = self.bindings[key]
    if acts and acts[action] then return true end
  end
  return false
end

function Input:pressed(action) return any(self, self.pressed_keys, action) end
function Input:released(action) return any(self, self.released_keys, action) end
function Input:down(action) return any(self, self.held, action) end

function Input:endFrame()
  self.pressed_keys = {}
  self.released_keys = {}
end

return Input
