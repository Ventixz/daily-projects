#!/usr/bin/env lua5.4
-- Loads a real LC-3 .obj file (big-endian words, load address first) and
-- runs it with real stdin/stdout, until it hits HALT.

local script_dir = (arg[0]:match("(.*/)") or "./")
local vm_mod = dofile(script_dir .. "../src/vm.lua")

local objfile = arg[1]
if not objfile then
  io.stderr:write("usage: lc3.lua <program.obj>\n")
  os.exit(1)
end

local f = assert(io.open(objfile, "rb"))
local data = f:read("a")
f:close()

local function word_at(byte_index)
  return (data:byte(byte_index + 1) << 8) | data:byte(byte_index + 2)
end

local orig = word_at(0)
local words = {}
local nwords = (#data // 2) - 1
for i = 1, nwords do
  words[i] = word_at(i * 2)
end

local vm = vm_mod.new_vm(orig, words)
-- A safety cap, not a real limit: a correct program halts on its own via
-- the HALT trap long before this, but a buggy one shouldn't hang forever.
vm_mod.run(vm, 10000000)
