-- Tiny test runner: lua tests/run.lua  (from the project folder)
package.path = "./?.lua;" .. package.path

local Object = require("lib.class")
local Timer = require("lib.timer")
local Input = require("lib.input")
local GameObject = require("engine.gameobject")
local Area = require("engine.area")
local Rooms = require("engine.rooms")
local FixedLoop = require("engine.loop")
local Player = require("game.player")
local Projectile = require("game.projectile")
local Game = require("game.init")
local Headless = require("headless")

local tests, passed, failed = {}, 0, 0
local function test(name, fn) tests[#tests + 1] = { name = name, fn = fn } end
local function eq(a, b, msg)
  if a ~= b then error(("%s: expected %s, got %s"):format(msg or "eq", tostring(b), tostring(a)), 2) end
end
local function near(a, b, msg, eps)
  if math.abs(a - b) > (eps or 1e-6) then
    error(("%s: expected ~%s, got %s"):format(msg or "near", tostring(b), tostring(a)), 2)
  end
end
local function ok(v, msg) if not v then error(msg or "expected truthy", 2) end end

-- A stand-in room for Area tests.
local function fakeRoom() return { w = 480, h = 270 } end

test("class: init, inheritance, super and is()", function()
  local Animal = Object:extend("Animal")
  function Animal:init(name) self.name = name end
  function Animal:speak() return "..." end
  local Dog = Animal:extend("Dog")
  function Dog:init(name) Dog.super.init(self, name); self.tricks = 0 end
  function Dog:speak() return "woof" end
  local d = Dog("rex")
  eq(d.name, "rex"); eq(d.tricks, 0); eq(d:speak(), "woof")
  ok(d:is(Dog) and d:is(Animal) and d:is(Object), "is() walks the chain")
  ok(not Animal("cat"):is(Dog), "parent is not a child")
  eq(tostring(d), "Dog", "__tostring copied to subclasses")
end)

test("timer: after fires once at the right time", function()
  local t, n = Timer(), 0
  t:after(0.5, function() n = n + 1 end)
  t:update(0.3); eq(n, 0, "too early")
  t:update(0.3); eq(n, 1, "fired")
  t:update(1); eq(n, 1, "only once"); eq(t:count(), 0)
end)

test("timer: tags replace and cancel", function()
  local t, log = Timer(), {}
  t:after(1, function() log[#log + 1] = "a" end, "x")
  t:after(1, function() log[#log + 1] = "b" end, "x")   -- replaces "a"
  t:after(1, function() log[#log + 1] = "c" end, "y")
  t:cancel("y")
  t:update(1)
  eq(table.concat(log), "b")
end)

test("timer: every with count, catching up on a long frame", function()
  local t, n = Timer(), 0
  t:every(0.1, function() n = n + 1 end, 5)
  t:update(0.35); eq(n, 3, "three intervals in one frame")
  t:update(1); eq(n, 5, "stops at count"); eq(t:count(), 0)
end)

test("timer: callbacks may add and cancel timers mid-update", function()
  local t, log = Timer(), {}
  t:after(0.1, function()
    log[#log + 1] = "first"
    t:cancel("second")
    t:after(0.1, function() log[#log + 1] = "spawned" end)
  end)
  t:after(0.1, function() log[#log + 1] = "second" end, "second")
  t:update(0.1); eq(table.concat(log, ","), "first")
  t:update(0.1); eq(table.concat(log, ","), "first,spawned")
end)

test("timer: tween eases and lands exactly on target", function()
  local t, s, done = Timer(), { x = 0, y = 10 }, false
  t:tween(1, s, { x = 100, y = 0 }, "in-quad", function() done = true end)
  t:update(0.5); near(s.x, 25, "in-quad at half time"); near(s.y, 7.5)
  t:update(0.7); eq(s.x, 100); eq(s.y, 0); ok(done, "after callback ran")
  near(Timer.easing["in-out-cubic"](0.5), 0.5, "cubic midpoint")
end)

test("input: pressed/released last one frame, down persists", function()
  local i = Input()
  i:bind("space", "shoot"); i:bind("j", "shoot")
  i:keypressed("space")
  ok(i:pressed("shoot") and i:down("shoot"), "pressed on first frame")
  i:endFrame()
  ok(not i:pressed("shoot") and i:down("shoot"), "held afterwards")
  i:keypressed("space")                          -- OS key repeat
  ok(not i:pressed("shoot"), "repeat is not a new press")
  i:keyreleased("space")
  ok(i:released("shoot") and not i:down("shoot"), "released")
  i:endFrame(); ok(not i:released("shoot"))
  ok(not i:down("unbound"), "unknown action is false")
end)

test("area: dead objects are removed and destroyed", function()
  local area = Area(fakeRoom())
  local a = area:addGameObject(GameObject, 1, 1)
  local b = area:addGameObject(GameObject, 2, 2)
  local timer = b.timer
  b.timer:after(1, function() error("should never fire") end)
  b.dead = true
  area:update(0.016)
  eq(#area.game_objects, 1); eq(area.game_objects[1], a)
  eq(b.area, nil, "references broken"); eq(timer:count(), 0, "timers cleared")
end)

test("area: objects spawned during update start next frame", function()
  local area = Area(fakeRoom())
  local Spawner = GameObject:extend("Spawner")
  local child_updates = 0
  local Child = GameObject:extend("Child")
  function Child:update(dt) child_updates = child_updates + 1 end
  function Spawner:update(dt) self.area:addGameObject(Child, 0, 0); self.dead = true end
  area:addGameObject(Spawner, 0, 0)
  area:update(0.016)
  eq(#area.game_objects, 1, "spawner gone, child present"); eq(child_updates, 0)
  area:update(0.016); eq(child_updates, 1)
end)

test("area: circle queries filter by class and distance", function()
  local area = Area(fakeRoom())
  local Enemy = GameObject:extend("Enemy")
  local Boss = Enemy:extend("Boss")
  area:addGameObject(GameObject, 0, 0)
  local near_e = area:addGameObject(Enemy, 3, 4)       -- distance 5
  local boss = area:addGameObject(Boss, 0, 9)          -- distance 9
  area:addGameObject(Enemy, 30, 0)                     -- out of range
  eq(#area:queryCircleArea(0, 0, 10), 3, "everything within r")
  eq(#area:queryCircleArea(0, 0, 10, { Enemy }), 2, "subclasses match")
  eq(#area:queryCircleArea(0, 0, 10, { Boss }), 1)
  eq(area:getClosestObject(0, 0, 10, { Enemy }), near_e)
  eq(area:getClosestObject(0, 10, 10, { Enemy }), boss)
  eq(area:getClosestObject(100, 100, 5), nil)
end)

test("area: draw order is depth then creation order", function()
  local area = Area(fakeRoom())
  local a = area:addGameObject(GameObject, 0, 0, { depth = 60 })
  local b = area:addGameObject(GameObject, 0, 0, { depth = 10 })
  local c = area:addGameObject(GameObject, 0, 0, { depth = 60 })
  local order = area:drawOrder()
  eq(order[1], b); eq(order[2], a); eq(order[3], c)
end)

test("rooms: fresh rooms are rebuilt, persistent rooms reused", function()
  local destroyed = 0
  local Fresh = Object:extend("Fresh")
  function Fresh:init(rooms, n) self.n = n end
  function Fresh:destroy() destroyed = destroyed + 1 end
  local Menu = Object:extend("Menu")
  Menu.persistent = true
  function Menu:init() self.visits = 0 end
  function Menu:enter() self.visits = self.visits + 1 end
  function Menu:destroy() error("persistent rooms are not destroyed") end

  local rooms = Rooms()
  rooms:register("fresh", Fresh); rooms:register("menu", Menu)
  local f1 = rooms:gotoRoom("fresh", 1)
  local m1 = rooms:gotoRoom("menu")
  eq(destroyed, 1, "leaving a fresh room destroys it")
  local f2 = rooms:gotoRoom("fresh", 2)
  ok(f1 ~= f2, "new instance"); eq(f2.n, 2, "args forwarded")
  local m2 = rooms:gotoRoom("menu")
  eq(m1, m2, "same instance"); eq(m2.visits, 2)
end)

test("rooms: request() switches after the current update", function()
  local rooms = Rooms()
  local A = Object:extend("A")
  function A:update() rooms:request("b", nil, "x") ; self.updated = true end
  local B = Object:extend("B")
  function B:init(_, a, b) self.a, self.b = a, b end
  rooms:register("a", A); rooms:register("b", B)
  local a = rooms:gotoRoom("a")
  rooms:update(0.016)
  ok(a.updated, "a finished its update")
  eq(rooms.current_name, "b"); eq(rooms.current.a, nil); eq(rooms.current.b, "x", "nil args kept")
end)

test("loop: fixed steps, leftover alpha and spiral-of-death clamp", function()
  local loop, steps = FixedLoop(0.01, 0.1), 0
  local n, alpha = loop:advance(0.035, function(dt) eq(dt, 0.01); steps = steps + 1 end)
  eq(n, 3); near(alpha, 0.5, "alpha", 1e-6)
  n = loop:advance(0.005, function() steps = steps + 1 end)
  eq(n, 1, "leftover carried over")
  n = loop:advance(5.0, function() steps = steps + 1 end)
  eq(n, 10, "a 5s stall is clamped to max_frame")
  local l60, c = FixedLoop(1 / 60), 0
  for _ = 1, 60 do l60:advance(1 / 60, function() c = c + 1 end) end
  eq(c, 60, "float error doesn't drop steps")
end)

-- Player tests drive a real Stage through the room manager.
local function newStage()
  local rooms, input = Game.new()
  return rooms.current, input, rooms
end

test("player: turns, accelerates to max speed and moves forward", function()
  local stage, input = newStage()
  local p = stage.player
  local y0 = p.y
  for _ = 1, 60 do stage:update(1 / 60) end
  near(p.v, 100, "capped at base max_v")
  ok(p.y < y0 - 40, "moved up (initial heading)")
  input:keypressed("right")
  local r0 = p.r
  for _ = 1, 30 do stage:update(1 / 60) end
  near(p.r - r0, 1.66 * math.pi * 0.5, "turned rv * 0.5s", 1e-9)
end)

test("player: boost raises max speed, brake clamps it", function()
  local stage, input = newStage()
  local p = stage.player
  p.timer:cancel("shoot")
  input:keypressed("up")
  for _ = 1, 120 do stage:update(1 / 60); p.y = 135 end   -- pin y to stay in bounds
  near(p.v, 150, "boosted max_v"); ok(p.boosting)
  input:keyreleased("up"); input:keypressed("s")
  stage:update(1 / 60)
  near(p.v, 50, "brake clamps instantly"); ok(not p.boosting)
end)

test("player: auto-fires and projectiles die off-screen", function()
  local stage = newStage()
  local p = stage.player
  for _ = 1, 30 do stage:update(1 / 60); p.y = 135 end      -- 0.5s
  eq(p.shots, 2, "one shot per 0.24s")
  eq(stage:projectileCount(), 2)
  p.timer:cancel("shoot")
  for _ = 1, 120 do stage:update(1 / 60); p.y = 135 end     -- 2s at 200px/s
  eq(stage:projectileCount(), 0, "all left the 270px-high area")
  for _, o in ipairs(stage.area.game_objects) do ok(not o:is(Projectile)) end
end)

test("player: leaving the area kills it and the stage restarts", function()
  local rooms = Game.new({ restart_delay = 0.5 })
  local first = rooms.current
  first.player.x = -1
  rooms:update(1 / 60)
  ok(first.player.dead, "dead"); eq(rooms.current, first, "not restarted yet")
  for _ = 1, 31 do rooms:update(1 / 60) end
  ok(rooms.current ~= first, "new stage"); eq(rooms.current.run, 2)
  eq(#first.area.game_objects, 0, "old stage destroyed")
  ok(not rooms.current.player.dead)
end)

test("headless: same script gives identical results (deterministic)", function()
  local a, b = Headless.run(), Headless.run()
  ok(a.ticks > 400, "simulation ran")
  eq(a.ticks, b.ticks); eq(a.x, b.x); eq(a.y, b.y); eq(a.r, b.r); eq(a.shots, b.shots)
  eq(a.run, 2, "default script ends after one death and restart")
end)

for _, t in ipairs(tests) do
  local good, err = pcall(t.fn)
  if good then
    passed = passed + 1
    print("ok   " .. t.name)
  else
    failed = failed + 1
    print("FAIL " .. t.name .. "\n     " .. tostring(err))
  end
end
print(("\n%d passed, %d failed"):format(passed, failed))
os.exit(failed == 0 and 0 or 1)
