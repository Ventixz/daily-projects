local script_dir = (arg[0]:match("(.*/)") or "./")
local asm = dofile(script_dir .. "../src/asm.lua")
local vm_mod = dofile(script_dir .. "../src/vm.lua")
local t = dofile(script_dir .. "harness.lua")

print("test_asm.lua")

t.test("assembles a single ADD immediate to the exact instruction word", function()
  local result = asm.assemble([[
    .ORIG x3000
    ADD R0, R0, #5
    .END
  ]])
  t.eq(result.orig, 0x3000, "orig")
  t.eq(result.words[1], 0x1025, "encoded word")
end)

t.test("resolves a forward-referenced label into a correct PCoffset9", function()
  -- BR TARGET is at x3000 (1 word), so pc-after-fetch is x3001; TARGET
  -- is defined two lines down, after one skipped ADD, at x3002 -- so
  -- the encoded offset needs to be 1, and it can't be known until pass 1
  -- has walked far enough to see where TARGET actually lands.
  local result = asm.assemble([[
    .ORIG x3000
    BR TARGET
    ADD R0, R0, #1
TARGET  ADD R0, R0, #2
    .END
  ]])
  t.eq(result.words[1] & 0x1FF, 1, "pcoffset9")
  t.eq(result.labels["TARGET"], 0x3002, "resolved label address")
end)

t.test(".STRINGZ lays out one byte per word plus a zero terminator", function()
  local result = asm.assemble([[
    .ORIG x3000
    .STRINGZ "Hi"
    .END
  ]])
  t.eq(#result.words, 3, "word count") -- 'H', 'i', 0
  t.eq(result.words[1], string.byte("H"), "H")
  t.eq(result.words[2], string.byte("i"), "i")
  t.eq(result.words[3], 0, "terminator")
end)

t.test(".BLKW reserves N zeroed words", function()
  local result = asm.assemble([[
    .ORIG x3000
    .BLKW 3
    ADD R0, R0, #7
    .END
  ]])
  t.eq(#result.words, 4, "word count")
  t.eq(result.words[4], 0x1027, "instruction after the reserved block")
end)

t.test("assembles and runs hello.asm end to end through PUTS", function()
  local f = assert(io.open(script_dir .. "../programs/hello.asm", "r"))
  local source = f:read("a")
  f:close()

  local result = asm.assemble(source)
  local out = {}
  local vm = vm_mod.new_vm(result.orig, result.words, {
    write_char = function(ch) table.insert(out, string.char(ch)) end,
  })
  vm_mod.run(vm, 1000)
  t.eq(table.concat(out), "Hello, LC-3!\n", "program output")
  t.eq(vm.running, false, "halted")
end)

t.test("assembles and runs sum10.asm: a real loop with a backward branch", function()
  local f = assert(io.open(script_dir .. "../programs/sum10.asm", "r"))
  local source = f:read("a")
  f:close()

  local result = asm.assemble(source)
  local vm = vm_mod.new_vm(result.orig, result.words)
  local steps = vm_mod.run(vm, 1000)
  t.eq(vm.reg[1], 55, "R1 (sum of 1..10)")
  t.eq(vm.reg[2], 10, "R2 (final counter)")
  t.eq(vm.running, false, "halted")
  if steps >= 1000 then error("hit the step cap -- loop likely never exits") end
end)

return t.summary()
