#!/usr/bin/env lua5.4
-- Assembles a .asm file into a real LC-3 object file: a flat sequence of
-- big-endian 16-bit words, with the load address as the first word --
-- the same format the original LC-3 toolchain's `.obj` files use.

local script_dir = (arg[0]:match("(.*/)") or "./")
local asm = dofile(script_dir .. "../src/asm.lua")

local infile, outfile = arg[1], arg[2]
if not infile or not outfile then
  io.stderr:write("usage: lc3as.lua <in.asm> <out.obj>\n")
  os.exit(1)
end

local f = assert(io.open(infile, "r"))
local source = f:read("a")
f:close()

local result = asm.assemble(source)

local out = assert(io.open(outfile, "wb"))
local function write_word(w)
  w = w & 0xFFFF
  out:write(string.char((w >> 8) & 0xFF, w & 0xFF))
end
write_word(result.orig)
for _, w in ipairs(result.words) do write_word(w) end
out:close()

print(string.format("assembled %s -> %s (orig=x%04X, %d words)", infile, outfile, result.orig, #result.words))
