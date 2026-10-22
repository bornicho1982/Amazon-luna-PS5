import struct
import capstone

with open('apps/luna-ps5/ps5/obj/llvm-pie.elf', 'rb') as f:
    data = f.read()

text_offset = 0x4000
crash_va = 0x918c
start_va = 0x8ff0

code = data[text_offset + start_va : text_offset + start_va + 612]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for i in md.disasm(code, start_va):
    marker = ' <=== CRASH (backtrace[0])' if i.address == crash_va else ''
    print(f'0x{i.address:04x}: {i.mnemonic:8s} {i.op_str}{marker}')
