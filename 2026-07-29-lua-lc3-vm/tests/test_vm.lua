-- Each test hand-encodes one raw LC-3 instruction word and steps the VM
-- once, then asserts on exact register/memory/condition-code state --
-- the same "drive it one instruction at a time" approach a CPU emulator
-- needs, since there's no compiler here to trust to emit the right bits.

local script_dir = (arg[0]:match("(.*/)") or "./")
local vm_mod = dofile(script_dir .. "../src/vm.lua")
local t = dofile(script_dir .. "harness.lua")

local function fresh(mem_words, orig)
  orig = orig or 0x3000
  local vm = vm_mod.new_vm(orig, mem_words)
  return vm
end

print("test_vm.lua")

t.test("ADD immediate sets register and POS flag", function()
  local vm = fresh({ 0x1025 }) -- ADD R0, R0, #5
  vm_mod.step(vm)
  t.eq(vm.reg[0], 5, "R0")
  t.eq(vm.cond, vm_mod.FL_POS, "cond")
end)

t.test("ADD register mode adds two registers", function()
  local vm = fresh({ 0x1025, 0x1263, 0x1401 }) -- R0=5; R1=3; R2 = R0+R1
  vm_mod.run(vm, 3)
  t.eq(vm.reg[2], 8, "R2")
end)

t.test("AND immediate #0 clears a register and sets ZRO", function()
  local vm = fresh({ 0x1025, 0x5020 }) -- R0=5; R0 = R0 AND 0
  vm_mod.run(vm, 2)
  t.eq(vm.reg[0], 0, "R0")
  t.eq(vm.cond, vm_mod.FL_ZRO, "cond")
end)

t.test("NOT complements bits and sets NEG when the sign bit is 1", function()
  local vm = fresh({ 0x923F }) -- NOT R1, R0 (R0 starts 0)
  vm_mod.step(vm)
  t.eq(vm.reg[1], 0xFFFF, "R1")
  t.eq(vm.cond, vm_mod.FL_NEG, "cond")
end)

t.test("BR only branches when its flag bits match the condition register", function()
  local vm = fresh({ 0x0805 }) -- BRn #5
  vm.cond = vm_mod.FL_NEG
  vm_mod.step(vm)
  t.eq(vm.pc, 0x3006, "pc after taken branch") -- 0x3000 + 1 (fetch) + 5 (offset)

  local vm2 = fresh({ 0x0805 })
  vm2.cond = vm_mod.FL_POS
  vm_mod.step(vm2)
  t.eq(vm2.pc, 0x3001, "pc after not-taken branch")
end)

t.test("JMP sets pc to the base register's value", function()
  local vm = fresh({ 0xC0C0 }) -- JMP R3
  vm.reg[3] = 0x4000
  vm_mod.step(vm)
  t.eq(vm.pc, 0x4000, "pc")
end)

t.test("JSR saves the return address in R7 and jumps PC-relative", function()
  local vm = fresh({ 0x480A }) -- JSR #10
  vm_mod.step(vm)
  t.eq(vm.reg[7], 0x3001, "R7 (return address)")
  t.eq(vm.pc, 0x300B, "pc")
end)

t.test("LD reads a PC-relative address", function()
  local vm = fresh({ 0x200F }) -- LD R0, #15  (0x3001 + 15 = 0x3010)
  vm.mem[0x3010] = 0x1234
  vm_mod.step(vm)
  t.eq(vm.reg[0], 0x1234, "R0")
end)

t.test("LDI dereferences a pointer stored at a PC-relative address", function()
  local vm = fresh({ 0xA20F }) -- LDI R1, #15
  vm.mem[0x3010] = 0x4000
  vm.mem[0x4000] = 0xABCD
  vm_mod.step(vm)
  t.eq(vm.reg[1], 0xABCD, "R1")
end)

t.test("LDR reads at base register + offset6", function()
  local vm = fresh({ 0x6684 }) -- LDR R3, R2, #4
  vm.reg[2] = 0x3050
  vm.mem[0x3054] = 0x0042
  vm_mod.step(vm)
  t.eq(vm.reg[3], 0x0042, "R3")
end)

t.test("LEA computes an address without touching memory", function()
  local vm = fresh({ 0xE814 }) -- LEA R4, #20
  vm_mod.step(vm)
  t.eq(vm.reg[4], 0x3015, "R4")
  t.eq(vm.cond, vm_mod.FL_POS, "cond")
end)

t.test("ST writes a register to a PC-relative address", function()
  local vm = fresh({ 0x3A1F }) -- ST R5, #31
  vm.reg[5] = 0x2222
  vm_mod.step(vm)
  t.eq(vm.mem[0x3020], 0x2222, "mem[0x3020]")
end)

t.test("STI writes through a pointer stored at a PC-relative address", function()
  local vm = fresh({ 0xBC0F }) -- STI R6, #15
  vm.mem[0x3010] = 0x4050
  vm.reg[6] = 0x99
  vm_mod.step(vm)
  t.eq(vm.mem[0x4050], 0x99, "mem[0x4050]")
end)

t.test("STR writes at base register + offset6", function()
  local vm = fresh({ 0x7204 }) -- STR R1, R0, #4
  vm.reg[0] = 0x3080
  vm.reg[1] = 0x77
  vm_mod.step(vm)
  t.eq(vm.mem[0x3084], 0x77, "mem[0x3084]")
end)

t.test("TRAP HALT stops the run loop", function()
  local vm = fresh({ 0xF025 })
  vm_mod.step(vm)
  t.eq(vm.running, false, "running")
end)

t.test("TRAP OUT writes through vm.io, not real stdout", function()
  local out = {}
  local vm = vm_mod.new_vm(0x3000, { 0xF021 }, { write_char = function(ch) table.insert(out, string.char(ch)) end })
  vm.reg[0] = string.byte("A")
  vm_mod.step(vm)
  t.eq(table.concat(out), "A", "captured output")
end)

t.test("TRAP PUTS prints memory as characters until a zero word", function()
  local out = {}
  local vm = vm_mod.new_vm(0x3000, { 0xF022 }, { write_char = function(ch) table.insert(out, string.char(ch)) end })
  vm.reg[0] = 0x4000
  vm.mem[0x4000] = string.byte("H")
  vm.mem[0x4001] = string.byte("i")
  vm.mem[0x4002] = 0
  vm_mod.step(vm)
  t.eq(table.concat(out), "Hi", "captured output")
end)

t.test("run() executes until HALT and reports the step count", function()
  local vm = fresh({ 0x1021, 0x1021, 0xF025 }) -- ADD R0,#1 twice, then HALT
  local steps = vm_mod.run(vm)
  t.eq(steps, 3, "steps")
  t.eq(vm.reg[0], 2, "R0")
  t.eq(vm.running, false, "running")
end)

t.test("run() honors a max_steps safety cap on a program that never halts", function()
  local vm = fresh({ 0x0FFF }) -- BRnzp -1 : an infinite loop back to itself
  local steps = vm_mod.run(vm, 50)
  t.eq(steps, 50, "steps")
  t.eq(vm.running, true, "running (never hit HALT)")
end)

return t.summary()
