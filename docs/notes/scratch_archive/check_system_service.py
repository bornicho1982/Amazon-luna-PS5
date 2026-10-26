#!/usr/bin/env python3
import struct

def dump_symbols(so_path, pattern):
    with open(so_path, "rb") as f:
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

    for s in sections:
        if s['type'] in (2, 11):
            strtab = data[sections[s['link']]['offset']:sections[s['link']]['offset'] + sections[s['link']]['size']]
            count = s['size'] // s['entsize']
            for i in range(count):
                st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", data, s['offset'] + i * s['entsize'])
                name = strtab[st_name:strtab.find(b'\0', st_name)].decode('latin1', errors='replace')
                if pattern.lower() in name.lower():
                    print(f"[{so_path}] {name} (val=0x{st_value:x}, size={st_size})")

dump_symbols("C:/ps5-payload-sdk/target/lib/libSceSystemService.so", "WebBrowser")
dump_symbols("C:/ps5-payload-sdk/target/lib/libSceUserService.so", "UserService")
