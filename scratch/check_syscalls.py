import os, sys, struct
import capstone

root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
pie_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'apps', 'luna-ps5', 'ps5', 'obj', 'llvm-pie.elf')

if not os.path.exists(pie_path):
    print(f"Error: {pie_path} not found")
    sys.exit(1)

with open(pie_path, 'rb') as f:
    data = f.read()

e_shoff, = struct.unpack('<Q', data[40:48])
e_shentsize, e_shnum, e_shstrndx = struct.unpack('<HHH', data[58:64])
sections = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh = struct.unpack('<IIQQQQIIQQ', data[off:off+64])
    sections.append({'name_idx': sh[0], 'type': sh[1], 'flags': sh[2], 'addr': sh[3], 'offset': sh[4], 'size': sh[5], 'link': sh[6], 'entsize': sh[9]})
shstrtab = data[sections[e_shstrndx]['offset']:sections[e_shstrndx]['offset']+sections[e_shstrndx]['size']]
for s in sections:
    end = shstrtab.find(b'\x00', s['name_idx'])
    s['name'] = shstrtab[s['name_idx']:end].decode('latin1')

text_sec = [s for s in sections if s['name'] == '.text'][0]
print(f".text: addr=0x{text_sec['addr']:x}, off=0x{text_sec['offset']:x}, size=0x{text_sec['size']:x}")
text_data = data[text_sec['offset']:text_sec['offset']+text_sec['size']]

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
syscalls = []
for ins in md.disasm(text_data, text_sec['addr']):
    if ins.mnemonic == 'syscall':
        syscalls.append((ins.address, ins.bytes.hex()))

print(f"Total actual syscall instructions in .text: {len(syscalls)}")
for a, b in syscalls[:10]:
    print(f"  0x{a:x}: {b}")

if len(syscalls) > 0:
    sys.exit(1)
sys.exit(0)
