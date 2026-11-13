-- Fixed-timestep loop ("Fix Your Timestep"). Real frame times vary; the
-- simulation always advances in equal `step` slices so physics is
-- deterministic. Leftover time stays in the accumulator for next frame, and
-- `alpha` (0..1) says how far we are into the next step, for interpolated
-- drawing. Frame time is clamped so one long stall can't trigger a spiral
-- of death where catching up takes longer than the stall itself.
local Object = require("lib.class")

local FixedLoop = Object:extend("FixedLoop")

function FixedLoop:init(step, max_frame)
  self.step = step or 1 / 60
  self.max_frame = max_frame or 0.25
  self.accumulator = 0
  self.ticks = 0
end

-- Returns the number of steps run and the interpolation alpha.
function FixedLoop:advance(frame_dt, update)
  self.accumulator = self.accumulator + math.min(frame_dt, self.max_frame)
  local steps = 0
  -- Small epsilon so 1/60 + 1/60 + 1/60 still counts as three steps.
  while self.accumulator + 1e-9 >= self.step do
    update(self.step)
    self.accumulator = self.accumulator - self.step
    steps = steps + 1
    self.ticks = self.ticks + 1
  end
  if self.accumulator < 0 then self.accumulator = 0 end
  return steps, self.accumulator / self.step
end

return FixedLoop
