#!/usr/bin/env python3
import struct
import capstone

with open("apps/luna-ps5/ps5/obj/llvm-pie.elf", "rb") as f:
    data = f.read()

def disasm_func(name, addr, size):
    start = 0x4000 + addr
    code = data[start : start + size]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    print(f"\n=== Disassembly of {name} (0x{addr:x}, size={size}) ===")
    for ins in md.disasm(code, addr):
        print(f"0x{ins.address:06x}:  {ins.bytes.hex():16s}  {ins.mnemonic:8s} {ins.op_str}")

disasm_func("0x356370", 0x356370, 100)
