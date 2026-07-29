-- LC-3 virtual machine: 16-bit words, 8 general registers, a 3-value
-- condition-code register, and a fetch/decode/execute loop. No I/O happens
-- directly here -- every trap goes through vm.io, so tests can swap in a
-- mock and assert on captured output instead of real stdin/stdout.

local M = {}

local MASK16 = 0xFFFF

local FL_POS = 1
local FL_ZRO = 2
local FL_NEG = 4

M.MASK16 = MASK16
M.FL_POS = FL_POS
M.FL_ZRO = FL_ZRO
M.FL_NEG = FL_NEG

local function sign_extend(x, bits)
  x = x & ((1 << bits) - 1)
  if (x >> (bits - 1)) & 1 == 1 then
    x = x | (~0 << bits)
  end
  return x & MASK16
end
M.sign_extend = sign_extend

local function default_io()
  return {
    write_char = function(ch) io.write(string.char(ch & 0xFF)) end,
    write_str = function(s) io.write(s) end,
    read_char = function()
      local c = io.read(1)
      if c then return string.byte(c) end
      return 0
    end,
  }
end
M.default_io = default_io

-- mem is a sparse table (address -> word). A 64K address space doesn't
-- need 64K allocated slots when almost every program only touches a few
-- hundred of them; unset addresses just read back as zero.
function M.new_vm(orig, words, io_overrides)
  local vm = {
    mem = {},
    reg = {[0] = 0, [1] = 0, [2] = 0, [3] = 0, [4] = 0, [5] = 0, [6] = 0, [7] = 0},
    pc = orig or 0x3000,
    cond = FL_ZRO,
    running = true,
    io = io_overrides or default_io(),
  }
  if words then
    for i, w in ipairs(words) do
      vm.mem[(orig + i - 1) & MASK16] = w & MASK16
    end
  end
  return vm
end

function M.mem_read(vm, addr)
  return vm.mem[addr & MASK16] or 0
end

function M.mem_write(vm, addr, val)
  vm.mem[addr & MASK16] = val & MASK16
end

local function update_flags(vm, r)
  local v = vm.reg[r]
  if v == 0 then
    vm.cond = FL_ZRO
  elseif (v >> 15) & 1 == 1 then
    vm.cond = FL_NEG
  else
    vm.cond = FL_POS
  end
end

local function trap(vm, vect)
  if vect == 0x20 then -- GETC: read one char into R0, no echo
    vm.reg[0] = vm.io.read_char() & MASK16
  elseif vect == 0x21 then -- OUT: write low byte of R0
    vm.io.write_char(vm.reg[0] & 0xFF)
  elseif vect == 0x22 then -- PUTS: R0 points at a string, one char per word
    local addr = vm.reg[0]
    local w = M.mem_read(vm, addr)
    while w ~= 0 do
      vm.io.write_char(w & 0xFF)
      addr = addr + 1
      w = M.mem_read(vm, addr)
    end
  elseif vect == 0x23 then -- IN: prompt, read one char, echo it
    vm.io.write_str("Enter a character: ")
    local ch = vm.io.read_char()
    vm.io.write_char(ch)
    vm.reg[0] = ch & MASK16
  elseif vect == 0x24 then -- PUTSP: two packed chars per word, low byte first
    local addr = vm.reg[0]
    local w = M.mem_read(vm, addr)
    while w ~= 0 do
      vm.io.write_char(w & 0xFF)
      local hi = (w >> 8) & 0xFF
      if hi ~= 0 then vm.io.write_char(hi) end
      addr = addr + 1
      w = M.mem_read(vm, addr)
    end
  elseif vect == 0x25 then -- HALT
    vm.running = false
  else
    error(string.format("unhandled trap vector x%02X", vect))
  end
end

-- These traps are implemented as emulator built-ins rather than as real
-- trap-vector-table entries pointing at loaded OS routines -- the same
-- simplification the reference tutorial (justinmeiners.github.io/lc3-vm)
-- makes, and enough to teach what TRAP *does* without also writing an OS.
function M.step(vm)
  local instr = M.mem_read(vm, vm.pc)
  vm.pc = (vm.pc + 1) & MASK16
  local op = (instr >> 12) & 0xF

  if op == 0x1 then -- ADD
    local dr, sr1 = (instr >> 9) & 0x7, (instr >> 6) & 0x7
    if (instr >> 5) & 0x1 == 0 then
      local sr2 = instr & 0x7
      vm.reg[dr] = (vm.reg[sr1] + vm.reg[sr2]) & MASK16
    else
      local imm5 = sign_extend(instr & 0x1F, 5)
      vm.reg[dr] = (vm.reg[sr1] + imm5) & MASK16
    end
    update_flags(vm, dr)

  elseif op == 0x5 then -- AND
    local dr, sr1 = (instr >> 9) & 0x7, (instr >> 6) & 0x7
    if (instr >> 5) & 0x1 == 0 then
      local sr2 = instr & 0x7
      vm.reg[dr] = vm.reg[sr1] & vm.reg[sr2]
    else
      local imm5 = sign_extend(instr & 0x1F, 5)
      vm.reg[dr] = vm.reg[sr1] & imm5 & MASK16
    end
    update_flags(vm, dr)

  elseif op == 0x9 then -- NOT
    local dr, sr = (instr >> 9) & 0x7, (instr >> 6) & 0x7
    vm.reg[dr] = (~vm.reg[sr]) & MASK16
    update_flags(vm, dr)

  elseif op == 0x0 then -- BR
    local n, z, p = (instr >> 11) & 0x1, (instr >> 10) & 0x1, (instr >> 9) & 0x1
    local off9 = sign_extend(instr & 0x1FF, 9)
    if (n == 1 and vm.cond == FL_NEG) or (z == 1 and vm.cond == FL_ZRO) or (p == 1 and vm.cond == FL_POS) then
      vm.pc = (vm.pc + off9) & MASK16
    end

  elseif op == 0xC then -- JMP / RET (RET is just JMP R7)
    local base = (instr >> 6) & 0x7
    vm.pc = vm.reg[base]

  elseif op == 0x4 then -- JSR / JSRR
    vm.reg[7] = vm.pc
    if (instr >> 11) & 0x1 == 1 then
      local off11 = sign_extend(instr & 0x7FF, 11)
      vm.pc = (vm.pc + off11) & MASK16
    else
      local base = (instr >> 6) & 0x7
      vm.pc = vm.reg[base]
    end

  elseif op == 0x2 then -- LD
    local dr = (instr >> 9) & 0x7
    local off9 = sign_extend(instr & 0x1FF, 9)
    vm.reg[dr] = M.mem_read(vm, vm.pc + off9)
    update_flags(vm, dr)

  elseif op == 0xA then -- LDI
    local dr = (instr >> 9) & 0x7
    local off9 = sign_extend(instr & 0x1FF, 9)
    local addr = M.mem_read(vm, vm.pc + off9)
    vm.reg[dr] = M.mem_read(vm, addr)
    update_flags(vm, dr)

  elseif op == 0x6 then -- LDR
    local dr, base = (instr >> 9) & 0x7, (instr >> 6) & 0x7
    local off6 = sign_extend(instr & 0x3F, 6)
    vm.reg[dr] = M.mem_read(vm, vm.reg[base] + off6)
    update_flags(vm, dr)

  elseif op == 0xE then -- LEA
    local dr = (instr >> 9) & 0x7
    local off9 = sign_extend(instr & 0x1FF, 9)
    vm.reg[dr] = (vm.pc + off9) & MASK16
    update_flags(vm, dr)

  elseif op == 0x3 then -- ST
    local sr = (instr >> 9) & 0x7
    local off9 = sign_extend(instr & 0x1FF, 9)
    M.mem_write(vm, vm.pc + off9, vm.reg[sr])

  elseif op == 0xB then -- STI
    local sr = (instr >> 9) & 0x7
    local off9 = sign_extend(instr & 0x1FF, 9)
    local addr = M.mem_read(vm, vm.pc + off9)
    M.mem_write(vm, addr, vm.reg[sr])

  elseif op == 0x7 then -- STR
    local sr, base = (instr >> 9) & 0x7, (instr >> 6) & 0x7
    local off6 = sign_extend(instr & 0x3F, 6)
    M.mem_write(vm, vm.reg[base] + off6, vm.reg[sr])

  elseif op == 0xF then -- TRAP
    trap(vm, instr & 0xFF)

  else
    error(string.format("unknown opcode x%X at pc=x%04X", op, (vm.pc - 1) & MASK16))
  end
end

function M.run(vm, max_steps)
  local steps = 0
  while vm.running do
    M.step(vm)
    steps = steps + 1
    if max_steps and steps >= max_steps then break end
  end
  return steps
end

return M
