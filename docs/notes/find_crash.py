import struct

def find_symbol(elf_path, target_addr):
    with open(elf_path, 'rb') as f:
        data = f.read()
    
    if data[:4] != b'\x7fELF':
        print(f'{elf_path} is not an ELF file')
        return
        
    e_shoff = struct.unpack('<Q', data[40:48])[0]
    e_shentsize = struct.unpack('<H', data[58:60])[0]
    e_shnum = struct.unpack('<H', data[60:62])[0]
    e_shstrndx = struct.unpack('<H', data[62:64])[0]
    
    if e_shnum == 0 or e_shoff == 0:
        print(f'{elf_path}: no section headers')
        return

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
        
    for s in sections:
        if s['type'] in (2, 11): # SHT_SYMTAB, SHT_DYNSYM
            strtab_sec = sections[s['link']]
            strtab = data[strtab_sec['offset']:strtab_sec['offset']+strtab_sec['size']]
            def get_symname(idx):
                end = strtab.find(b'\x00', idx)
                return strtab[idx:end].decode('latin1', errors='ignore')
            
            num_syms = s['size'] // s['entsize']
            best_sym = None
            best_diff = 0xffffffff
            for j in range(num_syms):
                sym_off = s['offset'] + j * s['entsize']
                st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack('<IBBHQQ', data[sym_off:sym_off+24])
                sname = s['name']
                if st_value <= target_addr and (target_addr < st_value + max(st_size, 1)):
                    symname = get_symname(st_name)
                    print(f"Exact match in {sname}: {symname} (0x{st_value:x} + 0x{target_addr - st_value:x}, size {st_size})")
                elif st_value <= target_addr:
                    diff = target_addr - st_value
                    if diff < best_diff and st_value > 0:
                        best_diff = diff
                        best_sym = (get_symname(st_name), st_value, st_size)
            if best_sym and best_diff < 0x4000:
                print(f"Closest match in {s['name']}: {best_sym[0]} at 0x{best_sym[1]:x} + 0x{best_diff:x}")

print('--- Searching 0x918c (offset from 0x400000) ---')
find_symbol('apps/luna-ps5/ps5/obj/llvm-pie.elf', 0x918c)
print('--- Searching 0x40918c ---')
find_symbol('apps/luna-ps5/ps5/obj/llvm-pie.elf', 0x40918c)
