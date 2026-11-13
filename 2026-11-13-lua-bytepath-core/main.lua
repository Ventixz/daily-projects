-- LOVE entry point (love .). Logic is the same code headless.lua and the
-- tests drive; this file only forwards keys and draws at 2x scale.
local Game = require("game.init")
local FixedLoop = require("engine.loop")

local rooms, input, loop

function love.load()
  love.graphics.setLineStyle("rough")
  rooms, input = Game.new()
  loop = FixedLoop(1 / 60)
end

function love.keypressed(key)
  if key == "escape" then love.event.quit() end
  input:keypressed(key)
end

function love.keyreleased(key) input:keyreleased(key) end

function love.update(dt)
  loop:advance(dt, function(step) rooms:update(step) end)
  input:endFrame()
end

function love.draw()
  love.graphics.push()
  love.graphics.scale(2, 2)
  rooms:draw(love.graphics)
  love.graphics.pop()
  love.graphics.print(("run %d   shots %d"):format(rooms.current.run, rooms.current.player.shots), 8, 8)
end
