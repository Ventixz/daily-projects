#!/usr/bin/env python3
"""Hand-assembles digits.ch8: draws the font glyphs 0-9 left to right,
then halts in an infinite loop. No assembler needed -- CHIP-8 opcodes are
simple enough to emit by hand, one instruction at a time.

Run: python3 make_digits_rom.py
"""

import struct

Y = 16
SPACING = 7
opcodes = []

for digit in range(10):
    x = 2 + digit * SPACING
    opcodes.append(0x6000 | digit)        # LD V0, digit
    opcodes.append(0x6100 | x)            # LD V1, x
    opcodes.append(0x6200 | Y)            # LD V2, Y
    opcodes.append(0xF029)                # LD F, V0  (I = font glyph for digit)
    opcodes.append(0xD125)                # DRW V1, V2, 5

halt_addr = 0x200 + len(opcodes) * 2
opcodes.append(0x1000 | halt_addr)        # JP halt_addr  (spin forever)

with open("digits.ch8", "wb") as f:
    for op in opcodes:
        f.write(struct.pack(">H", op))

print(f"wrote digits.ch8: {len(opcodes)} instructions, {len(opcodes) * 2} bytes")
