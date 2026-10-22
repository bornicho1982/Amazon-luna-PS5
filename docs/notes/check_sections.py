import struct

with open('apps/luna-ps5/ps5/obj/llvm-pie.elf', 'rb') as f:
    data = f.read()

e_shoff = struct.unpack('<Q', data[40:48])[0]
e_shentsize = struct.unpack('<H', data[58:60])[0]
e_shnum = struct.unpack('<H', data[60:62])[0]
e_shstrndx = struct.unpack('<H', data[62:64])[0]

sections = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh = struct.unpack('<IIQQQQIIQQ', data[off:off+64])
    sections.append({
        'name_idx': sh[0], 'type': sh[1], 'flags': sh[2],
        'addr': sh[3], 'offset': sh[4], 'size': sh[5],
        'link': sh[6], 'info': sh[7], 'entsize': sh[9]
    })

shstrtab = data[sections[e_shstrndx]['offset']:sections[e_shstrndx]['offset']+sections[e_shstrndx]['size']]
def get_shname(idx):
    end = shstrtab.find(b'\x00', idx)
    return shstrtab[idx:end].decode('latin1')

for s in sections:
    s['name'] = get_shname(s['name_idx'])
    if s['name'] in ('.text', '.rodata'):
        print(f"{s['name']}: addr=0x{s['addr']:x}, offset=0x{s['offset']:x}, size=0x{s['size']:x}")
