-- Timer: after / every / tween, all driven by update(dt).
-- Every call takes an optional tag. Starting a timer with a tag that is
-- already running replaces the old one, and cancel(tag) stops it. That is
-- the pattern BYTEPATH leans on to avoid stacking duplicate timers.
local Object = require("lib.class")

local Timer = Object:extend("Timer")

Timer.easing = {
  linear = function(t) return t end,
  ["in-quad"] = function(t) return t * t end,
  ["out-quad"] = function(t) return t * (2 - t) end,
  ["in-out-cubic"] = function(t)
    if t < 0.5 then return 4 * t * t * t end
    local f = 2 * t - 2
    return 0.5 * f * f * f + 1
  end,
}

function Timer:init()
  self.handles = {}
  self.order = {}      -- tags in insertion order, so callbacks fire predictably
  self.next_id = 0
end

function Timer:_add(tag, handle)
  if tag == nil then
    self.next_id = self.next_id + 1
    tag = "__anon" .. self.next_id
  end
  if not self.handles[tag] then self.order[#self.order + 1] = tag end
  handle.elapsed = 0
  self.handles[tag] = handle
  return tag
end

function Timer:after(delay, fn, tag)
  return self:_add(tag, { kind = "after", delay = delay, fn = fn })
end

-- Run fn every `interval` seconds; stop after `count` runs (nil = forever).
function Timer:every(interval, fn, count, tag)
  assert(interval > 0, "every: interval must be > 0")
  return self:_add(tag, { kind = "every", delay = interval, fn = fn, count = count })
end

-- Interpolate numeric fields of `subject` towards `target`.
function Timer:tween(duration, subject, target, method, after, tag)
  local ease = Timer.easing[method or "linear"]
  assert(ease, "unknown easing: " .. tostring(method))
  local start = {}
  for k in pairs(target) do start[k] = subject[k] end
  return self:_add(tag, {
    kind = "tween", delay = duration, subject = subject, target = target,
    start = start, ease = ease, after = after,
  })
end

function Timer:cancel(tag) self.handles[tag] = nil end

function Timer:clear() self.handles, self.order = {}, {} end

function Timer:count()
  local n = 0
  for _ in pairs(self.handles) do n = n + 1 end
  return n
end

function Timer:update(dt)
  -- Iterate over a snapshot so callbacks may add or cancel timers safely.
  local snapshot = self.order
  self.order = {}
  for _, tag in ipairs(snapshot) do
    if self.handles[tag] then self.order[#self.order + 1] = tag end
  end

  for _, tag in ipairs(snapshot) do
    local h = self.handles[tag]
    if h then
      h.elapsed = h.elapsed + dt
      if h.kind == "after" then
        if h.elapsed >= h.delay then
          if self.handles[tag] == h then self.handles[tag] = nil end
          h.fn()
        end
      elseif h.kind == "every" then
        -- A long frame can cover several intervals: fire once per interval.
        while h.elapsed >= h.delay and self.handles[tag] == h do
          h.elapsed = h.elapsed - h.delay
          h.fn()
          if h.count then
            h.count = h.count - 1
            if h.count <= 0 then
              if self.handles[tag] == h then self.handles[tag] = nil end
              break
            end
          end
        end
      elseif h.kind == "tween" then
        local t = math.min(h.elapsed / h.delay, 1)
        local e = h.ease(t)
        for k, goal in pairs(h.target) do
          if t >= 1 then
            h.subject[k] = goal            -- land exactly, no float drift
          else
            h.subject[k] = h.start[k] + (goal - h.start[k]) * e
          end
        end
        if t >= 1 then
          if self.handles[tag] == h then self.handles[tag] = nil end
          if h.after then h.after() end
        end
      end
    end
  end

  -- Drop tags whose handles finished or were cancelled during this update.
  local live = {}
  for _, tag in ipairs(self.order) do
    if self.handles[tag] then live[#live + 1] = tag end
  end
  self.order = live
end

return Timer
