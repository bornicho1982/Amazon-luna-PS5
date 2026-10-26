#!/usr/bin/env python3
import struct
import capstone

with open("apps/luna-ps5/ps5/obj/llvm-pie.elf", "rb") as f:
    data = f.read()

# Segment 0 is at offset 0x4000
# So virtual address 0x220 is at file offset 0x4000 + 0x220 = 0x4220
start = 0x4000 + 0x220
code = data[start : start + 211]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
print("=== Disassembly of _Znwm (operator new) ===")
for ins in md.disasm(code, 0x220):
    marker = " <=== CRASH / TARGET (RIP: 0x4002ea)" if ins.address == 0x2ea else ""
    print(f"0x{ins.address:06x}:  {ins.bytes.hex():16s}  {ins.mnemonic:8s} {ins.op_str}{marker}")
