#!/usr/bin/env python3
import struct

with open("apps/luna-ps5/ps5/obj/llvm-pie.elf", "rb") as f:
    data = f.read()

e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHIQQQIHHHHHH", data, 16)
sections = []
for i in range(e_shnum):
    off = e_shoff + i * e_shentsize
    sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = struct.unpack_from("<IIQQQQIIQQ", data, off)
    sections.append({'index': i, 'name_idx': sh_name, 'type': sh_type, 'addr': sh_addr, 'offset': sh_offset, 'size': sh_size, 'link': sh_link, 'entsize': sh_entsize})

shstrtab = data[sections[e_shstrndx]['offset']:sections[e_shstrndx]['offset'] + sections[e_shstrndx]['size']]
for s in sections:
    s['name'] = shstrtab[s['name_idx']:shstrtab.find(b'\0', s['name_idx'])].decode('latin1')

syms = []
for s in sections:
    if s['type'] in (2, 11): # SHT_SYMTAB, SHT_DYNSYM
        strtab = data[sections[s['link']]['offset']:sections[s['link']]['offset'] + sections[s['link']]['size']]
        count = s['size'] // s['entsize']
        for i in range(count):
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", data, s['offset'] + i * s['entsize'])
            name = strtab[st_name:strtab.find(b'\0', st_name)].decode('latin1', errors='replace')
            syms.append({'name': name, 'val': st_value, 'size': st_size, 'type': st_info & 0xf})

print("--- Symbols containing or near 0x345c90 ---")
exact = [s for s in syms if s['val'] <= 0x345c90 < s['val'] + s['size']]
for s in exact:
    print(f"EXACT: 0x{s['val']:08x} (+{s['size']:5d}) type={s['type']} {s['name']}")

near = [s for s in syms if abs(s['val'] - 0x345c90) < 0x800]
near.sort(key=lambda s: s['val'])
for s in near:
    print(f"NEAR:  0x{s['val']:08x} (+{s['size']:5d}) type={s['type']} {s['name']}")

print("\n--- Symbols containing 0x2ea ---")
exact2 = [s for s in syms if s['val'] <= 0x2ea < s['val'] + s['size']]
for s in exact2:
    print(f"EXACT: 0x{s['val']:08x} (+{s['size']:5d}) type={s['type']} {s['name']}")

near2 = [s for s in syms if s['val'] <= 0x500]
near2.sort(key=lambda s: s['val'])
for s in near2[:20]:
    print(f"LOW:   0x{s['val']:08x} (+{s['size']:5d}) type={s['type']} {s['name']}")
