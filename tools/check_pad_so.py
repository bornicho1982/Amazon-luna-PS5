import struct

with open('C:/ps5-payload-sdk/target/lib/libScePad.so', 'rb') as f:
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
    if s['type'] in (2, 11):
        strtab_sec = sections[s['link']]
        strtab = data[strtab_sec['offset']:strtab_sec['offset']+strtab_sec['size']]
        num_syms = s['size'] // s['entsize']
        for j in range(num_syms):
            sym_off = s['offset'] + j * s['entsize']
            st_name = struct.unpack('<I', data[sym_off:sym_off+4])[0]
            end = strtab.find(b'\x00', st_name)
            name = strtab[st_name:end].decode('latin1')
            if name:
                print(name)
