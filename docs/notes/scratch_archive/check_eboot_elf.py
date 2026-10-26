#!/usr/bin/env python3
import struct
import capstone

def check_file(path):
    with open(path, "rb") as f:
        data = f.read()

    print(f"=== {path} ===")
    # Find text section or offset
    # In check_load.py, PT_LOAD 0 was offset=0x4000, vaddr=0x0
    # Let's check what is at offset 0x4000 + 0x2ea
    start_2ea = 0x4000 + 0x2ea - 10
    code_2ea = data[start_2ea : start_2ea + 30]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    print("Around 0x2ea:")
    for ins in md.disasm(code_2ea, 0x2ea - 10):
        marker = " <=== TARGET" if ins.address == 0x2ea else ""
        print(f"0x{ins.address:06x}: {ins.bytes.hex():16s} {ins.mnemonic:8s} {ins.op_str}{marker}")

    start_c90 = 0x4000 + 0x345c90 - 16
    code_c90 = data[start_c90 : start_c90 + 32]
    print("\nAround 0x345c90:")
    for ins in md.disasm(code_c90, 0x345c90 - 16):
        marker = " <=== TARGET" if ins.address == 0x345c90 else ""
        print(f"0x{ins.address:06x}: {ins.bytes.hex():16s} {ins.mnemonic:8s} {ins.op_str}{marker}")

check_file("apps/luna-ps5/ps5/obj/eboot.elf")
