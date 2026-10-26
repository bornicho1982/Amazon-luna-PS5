#!/usr/bin/env python3
import struct

def check_elf(path):
    with open(path, "rb") as f:
        data = f.read()
    print(f"=== {path} ===")
    e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags, e_ehsize, e_phentsize, e_phnum = struct.unpack_from("<HHIQQQIHHH", data, 16)
    print(f"Type: {e_type}, Entry: 0x{e_entry:x}, phoff: 0x{e_phoff:x}, phnum: {e_phnum}")
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from("<IIQQQQQQ", data, off)
        if p_type == 1: # PT_LOAD
            print(f"  PT_LOAD: offset=0x{p_offset:x}, vaddr=0x{p_vaddr:x}, filesz=0x{p_filesz:x}, memsz=0x{p_memsz:x}, flags=0x{p_flags:x}")

check_elf("apps/luna-ps5/ps5/obj/eboot.elf")
check_elf("apps/luna-ps5/ps5/obj/llvm-pie.elf")
