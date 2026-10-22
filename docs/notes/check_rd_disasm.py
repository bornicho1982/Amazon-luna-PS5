import capstone

with open('apps/luna-ps5/ps5/obj/llvm-pie.elf', 'rb') as f:
    data = f.read()

text_offset = 0x4000
start_va = 0x2dae0
size = 0x50

code = data[text_offset + start_va : text_offset + start_va + size]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

for i in md.disasm(code, start_va):
    print(f'0x{i.address:04x}: {i.mnemonic:8s} {i.op_str}')
