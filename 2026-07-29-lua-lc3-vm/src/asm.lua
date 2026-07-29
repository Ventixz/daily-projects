-- A two-pass assembler for a subset of LC-3 assembly. Pass 1 walks the
-- source just far enough to know how many words each line occupies, so
-- every label has a resolved address before pass 2 encodes a single
-- instruction -- which matters because a branch to a label defined further
-- down the file (a forward reference) can't be encoded until that address
-- is known.

local M = {}

local MASK16 = 0xFFFF

local OPCODE = {
  ADD = 0x1, AND = 0x5, NOT = 0x9,
  LD = 0x2, LDI = 0xA, LDR = 0x6, LEA = 0xE,
  ST = 0x3, STI = 0xB, STR = 0x7,
}

local TRAP_ALIAS = { GETC = 0x20, OUT = 0x21, PUTS = 0x22, IN = 0x23, PUTSP = 0x24, HALT = 0x25 }

local DIRECTIVE = { [".ORIG"] = true, [".FILL"] = true, [".STRINGZ"] = true, [".BLKW"] = true, [".END"] = true }

local function is_mnemonic(word)
  local u = word:upper():gsub(":$", "")
  if OPCODE[u] or TRAP_ALIAS[u] or DIRECTIVE[u] then return true end
  if u == "RET" or u == "JMP" or u == "JSR" or u == "JSRR" or u == "TRAP" then return true end
  if u:match("^BR[NZP]*$") then return true end
  return false
end

-- Splits one line of source into an optional label, a mnemonic, and its
-- operand tokens. .STRINGZ is special-cased because its one "operand" is a
-- quoted string that may itself contain commas and spaces.
local function parse_line(line)
  local semi = line:find(";")
  if semi then line = line:sub(1, semi - 1) end
  line = line:match("^%s*(.-)%s*$")
  if line == "" then return nil end

  local label = nil
  local first = line:match("^(%S+)")
  if first and not is_mnemonic(first) then
    label = first:gsub(":$", "")
    line = line:sub(#first + 1):match("^%s*(.-)%s*$")
  end
  if line == "" then return label, nil, {} end

  local mnemonic = line:match("^(%S+)"):upper()
  local rest = line:sub(#mnemonic + 1):match("^%s*(.-)%s*$")

  local operands = {}
  if mnemonic == ".STRINGZ" then
    operands = { rest:match('^"(.*)"$') }
  elseif rest ~= "" then
    for tok in rest:gmatch("[^,%s]+") do table.insert(operands, tok) end
  end
  return label, mnemonic, operands
end

local function parse_reg(tok)
  local n = tok and tok:upper():match("^R([0-7])$")
  if not n then error("expected a register (R0-R7), got '" .. tostring(tok) .. "'") end
  return tonumber(n)
end

-- A numeric literal ("#10", "#-3", "x3000") or a previously-resolved label.
local function value_of(tok, labels)
  local sign, digits = tok:match("^#(%-?%d+)$"), nil
  if sign then return tonumber(sign) end
  digits = tok:match("^[xX]([%x]+)$")
  if digits then return tonumber(digits, 16) end
  if tonumber(tok) then return tonumber(tok) end
  if labels[tok] then return labels[tok] end
  error("unresolved label or literal: '" .. tok .. "'")
end

local function encode(mnemonic, operands, addr, labels)
  local next_pc = addr + 1 -- PC has already advanced past this instruction when it executes

  local function pcoffset(tok, bits)
    local off = value_of(tok, labels) - next_pc
    return off & ((1 << bits) - 1)
  end

  if mnemonic == "ADD" or mnemonic == "AND" then
    local dr, sr1 = parse_reg(operands[1]), parse_reg(operands[2])
    local word = (OPCODE[mnemonic] << 12) | (dr << 9) | (sr1 << 6)
    local ok, sr2 = pcall(parse_reg, operands[3])
    if ok then
      return word | sr2
    else
      return word | (1 << 5) | (value_of(operands[3], labels) & 0x1F)
    end

  elseif mnemonic == "NOT" then
    local dr, sr = parse_reg(operands[1]), parse_reg(operands[2])
    return (0x9 << 12) | (dr << 9) | (sr << 6) | 0x3F

  elseif mnemonic:match("^BR") then
    local flags = mnemonic:sub(3)
    local n, z, p
    if flags == "" then n, z, p = 1, 1, 1
    else
      n = flags:find("N") and 1 or 0
      z = flags:find("Z") and 1 or 0
      p = flags:find("P") and 1 or 0
    end
    return (n << 11) | (z << 10) | (p << 9) | pcoffset(operands[1], 9)

  elseif mnemonic == "JMP" then
    return (0xC << 12) | (parse_reg(operands[1]) << 6)

  elseif mnemonic == "RET" then
    return (0xC << 12) | (7 << 6)

  elseif mnemonic == "JSR" then
    return (0x4 << 12) | (1 << 11) | pcoffset(operands[1], 11)

  elseif mnemonic == "JSRR" then
    return (0x4 << 12) | (parse_reg(operands[1]) << 6)

  elseif mnemonic == "LD" or mnemonic == "LDI" or mnemonic == "LEA" or mnemonic == "ST" or mnemonic == "STI" then
    local dr = parse_reg(operands[1])
    return (OPCODE[mnemonic] << 12) | (dr << 9) | pcoffset(operands[2], 9)

  elseif mnemonic == "LDR" or mnemonic == "STR" then
    local dr, base = parse_reg(operands[1]), parse_reg(operands[2])
    local off6 = value_of(operands[3], labels) & 0x3F
    return (OPCODE[mnemonic] << 12) | (dr << 9) | (base << 6) | off6

  elseif mnemonic == "TRAP" then
    return (0xF << 12) | (value_of(operands[1], labels) & 0xFF)

  elseif TRAP_ALIAS[mnemonic] then
    return (0xF << 12) | TRAP_ALIAS[mnemonic]

  else
    error("unknown mnemonic: " .. mnemonic)
  end
end

-- assemble(source) -> { orig = <load address>, words = { ... } }
function M.assemble(source)
  local orig, addr = nil, nil
  local labels = {}
  local entries = {}

  for line in (source .. "\n"):gmatch("(.-)\n") do
    local label, mnemonic, operands = parse_line(line)
    if mnemonic == ".ORIG" then
      orig = value_of(operands[1], labels)
      addr = orig
    elseif mnemonic == ".END" then
      break
    elseif mnemonic then
      if not addr then error("instruction before .ORIG") end
      if label then labels[label] = addr end
      local size = 1
      if mnemonic == ".BLKW" then
        size = value_of(operands[1], labels)
      elseif mnemonic == ".STRINGZ" then
        size = #operands[1] + 1
      end
      table.insert(entries, { mnemonic = mnemonic, operands = operands, addr = addr })
      addr = addr + size
    elseif label then
      labels[label] = addr
    end
  end

  if not orig then error("source has no .ORIG") end

  local mem = {}
  local max_addr = orig
  for _, e in ipairs(entries) do
    local a = e.addr
    if a > max_addr then max_addr = a end
    if e.mnemonic == ".FILL" then
      mem[a] = value_of(e.operands[1], labels) & MASK16
    elseif e.mnemonic == ".BLKW" then
      local n = value_of(e.operands[1], labels)
      for i = 0, n - 1 do mem[a + i] = 0 end
      if a + n - 1 > max_addr then max_addr = a + n - 1 end
    elseif e.mnemonic == ".STRINGZ" then
      local s = e.operands[1]:gsub("\\n", "\n")
      for i = 1, #s do mem[a + i - 1] = string.byte(s, i) end
      mem[a + #s] = 0
      if a + #s > max_addr then max_addr = a + #s end
    else
      mem[a] = encode(e.mnemonic, e.operands, a, labels) & MASK16
    end
  end

  local words = {}
  for a = orig, max_addr do
    words[#words + 1] = mem[a] or 0
  end
  return { orig = orig, words = words, labels = labels }
end

return M
