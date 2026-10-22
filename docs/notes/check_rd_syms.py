import struct

with open('apps/luna-ps5/ps5/obj/llvm-pie.elf', 'rb') as f:
    data = f.read()

e_shoff = struct.unpack('<Q', data[40:48])[0]
e_shentsize = struct.unpack('<H', data[58:60])[0]
e_shnum = struct.unpack('<H', data[60:62])[0]

sections = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh = struct.unpack('<IIQQQQIIQQ', data[off:off+64])
    sections.append({'type': sh[1], 'offset': sh[4], 'size': sh[5], 'link': sh[6], 'entsize': sh[9]})

for s in sections:
    if s['type'] == 2:
        strtab_sec = sections[s['link']]
        strtab = data[strtab_sec['offset']:strtab_sec['offset']+strtab_sec['size']]
        num_syms = s['size'] // s['entsize']
        for j in range(num_syms):
            sym_off = s['offset'] + j * s['entsize']
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack('<IBBHQQ', data[sym_off:sym_off+24])
            if st_value in (0x2dae0, 0x2daf0, 0x2db00):
                end = strtab.find(b'\x00', st_name)
                print(f"0x{st_value:x}: {strtab[st_name:end].decode('latin1')}")
