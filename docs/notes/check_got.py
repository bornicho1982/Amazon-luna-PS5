import struct

with open('apps/luna-ps5/ps5/obj/llvm-pie.elf', 'rb') as f:
    data = f.read()

# Let's inspect .rela.dyn or .rela.plt
e_shoff = struct.unpack('<Q', data[40:48])[0]
e_shentsize = struct.unpack('<H', data[58:60])[0]
e_shnum = struct.unpack('<H', data[60:62])[0]
e_shstrndx = struct.unpack('<H', data[62:64])[0]

sections = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh = struct.unpack('<IIQQQQIIQQ', data[off:off+64])
    sections.append({'name_idx': sh[0], 'type': sh[1], 'addr': sh[3], 'offset': sh[4], 'size': sh[5], 'link': sh[6], 'entsize': sh[9]})

shstrtab = data[sections[e_shstrndx]['offset']:sections[e_shstrndx]['offset']+sections[e_shstrndx]['size']]
def get_shname(idx):
    end = shstrtab.find(b'\x00', idx)
    return shstrtab[idx:end].decode('latin1')

for s in sections:
    s['name'] = get_shname(s['name_idx'])
    if 'rela' in s['name']:
        dynsym_sec = sections[s['link']]
        dynsym_strtab_sec = sections[dynsym_sec['link']]
        dynsym_strtab = data[dynsym_strtab_sec['offset']:dynsym_strtab_sec['offset']+dynsym_strtab_sec['size']]
        num_rela = s['size'] // s['entsize']
        for k in range(num_rela):
            r_off, r_info, r_addend = struct.unpack('<QQq', data[s['offset']+k*24 : s['offset']+(k+1)*24])
            if r_off == 0xdc478:
                sym_idx = r_info >> 32
                sym_off = dynsym_sec['offset'] + sym_idx * dynsym_sec['entsize']
                st_name = struct.unpack('<I', data[sym_off:sym_off+4])[0]
                end = dynsym_strtab.find(b'\x00', st_name)
                sym_name = dynsym_strtab[st_name:end].decode('latin1')
                print(f"GOT 0xdc478 relocation: {sym_name}")
