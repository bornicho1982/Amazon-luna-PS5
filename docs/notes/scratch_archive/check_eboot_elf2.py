#!/usr/bin/env python3
import struct
import capstone

with open("apps/luna-ps5/ps5/obj/eboot.elf", "rb") as f:
    data = f.read()

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

# Disassemble from 0x220
code = data[0x4000 + 0x220 : 0x4000 + 0x300]
print("=== eboot.elf from 0x220 ===")
for ins in md.disasm(code, 0x220):
    marker = " <=== CRASH / TARGET" if ins.address == 0x2ea else ""
    print(f"0x{ins.address:06x}: {ins.bytes.hex():16s} {ins.mnemonic:8s} {ins.op_str}{marker}")

# Disassemble from 0x345970
code_celt = data[0x4000 + 0x345970 : 0x4000 + 0x345d00]
print("\n=== eboot.elf from 0x345970 (clt_mdct_forward_c) ===")
for ins in md.disasm(code_celt, 0x345970):
    if 0x345c80 <= ins.address <= 0x345ca0:
        marker = " <=== TARGET" if ins.address == 0x345c90 else ""
        print(f"0x{ins.address:06x}: {ins.bytes.hex():16s} {ins.mnemonic:8s} {ins.op_str}{marker}")
