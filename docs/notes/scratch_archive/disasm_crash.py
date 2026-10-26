#!/usr/bin/env python3
import sys
import struct
import capstone

def parse_elf(filename):
    with open(filename, 'rb') as f:
        data = f.read()

    magic = data[:4]
    if magic != b'\x7fELF':
        raise ValueError("Not an ELF file")

    e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = struct.unpack_from('<IIQQQQIIQQ', data, off)
        sections.append({
            'index': i,
            'name_idx': sh_name,
            'type': sh_type,
            'flags': sh_flags,
            'addr': sh_addr,
            'offset': sh_offset,
            'size': sh_size,
            'link': sh_link,
            'info': sh_info,
            'entsize': sh_entsize
        })

    shstrtab = data[sections[e_shstrndx]['offset']:sections[e_shstrndx]['offset'] + sections[e_shstrndx]['size']]
    for s in sections:
        name_end = shstrtab.find(b'\0', s['name_idx'])
        s['name'] = shstrtab[s['name_idx']:name_end].decode('latin1')

    # Load symbol tables (.symtab and .dynsym)
    symbols = []
    for s in sections:
        if s['type'] in (2, 11): # SHT_SYMTAB, SHT_DYNSYM
            strtab_sec = sections[s['link']]
            strtab = data[strtab_sec['offset']:strtab_sec['offset'] + strtab_sec['size']]
            num_syms = s['size'] // s['entsize']
            for i in range(num_syms):
                sym_off = s['offset'] + i * s['entsize']
                st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from('<IBBHQQ', data, sym_off)
                name_end = strtab.find(b'\0', st_name)
                name = strtab[st_name:name_end].decode('latin1', errors='replace')
                symbols.append({
                    'name': name,
                    'value': st_value,
                    'size': st_size,
                    'info': st_info,
                    'shndx': st_shndx
                })

    return data, sections, symbols

def find_symbol_for_addr(symbols, addr):
    candidates = []
    for sym in symbols:
        if not sym['name']:
            continue
        # Exact range
        if sym['size'] > 0:
            if sym['value'] <= addr < sym['value'] + sym['size']:
                candidates.append((sym, 0, addr - sym['value']))
        else:
            # Maybe symbol size is 0
            if sym['value'] <= addr < sym['value'] + 0x1000:
                candidates.append((sym, 1, addr - sym['value']))
    if candidates:
        # sort by delta
        candidates.sort(key=lambda c: (c[1], c[2]))
        return candidates[0][0], candidates[0][2]
    # find closest preceding symbol
    preceding = [s for s in symbols if s['name'] and s['value'] <= addr]
    if preceding:
        preceding.sort(key=lambda s: addr - s['value'])
        return preceding[0], addr - preceding[0]['value']
    return None, 0

def disassemble_range(data, sections, start_addr, end_addr):
    # Find section containing start_addr
    target_sec = None
    for s in sections:
        if s['addr'] <= start_addr < s['addr'] + s['size']:
            target_sec = s
            break
    if not target_sec:
        print(f"Address 0x{start_addr:x} not found in any section!")
        return

    sec_offset = target_sec['offset'] + (start_addr - target_sec['addr'])
    code_bytes = data[sec_offset : sec_offset + (end_addr - start_addr)]

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    print(f"\n--- Disassembly 0x{start_addr:x} .. 0x{end_addr:x} (Section {target_sec['name']}) ---")
    for ins in md.disasm(code_bytes, start_addr):
        marker = " <=== CRASH / TARGET" if ins.address in (0x2ea, 0x345c90) else ""
        print(f"0x{ins.address:06x}:  {ins.bytes.hex():16s}  {ins.mnemonic:8s} {ins.op_str}{marker}")

def main():
    elf_path = "apps/luna-ps5/ps5/obj/llvm-pie.elf"
    print(f"Analyzing {elf_path}...")
    data, sections, symbols = parse_elf(elf_path)

    # Check 0x2ea and 0x345c90
    for target in [0x2ea, 0x345c90]:
        sym, delta = find_symbol_for_addr(symbols, target)
        if sym:
            print(f"\n[+] Address 0x{target:x} falls in symbol: '{sym['name']}' (value=0x{sym['value']:x}, size={sym['size']}) offset=+0x{delta:x}")
        else:
            print(f"\n[-] Address 0x{target:x} has no matching symbol.")

    # Disassemble 0x2c0..0x310
    disassemble_range(data, sections, 0x2c0, 0x310)

    # Disassemble 0x345c00..0x345ce0
    disassemble_range(data, sections, 0x345c00, 0x345ce0)

if __name__ == '__main__':
    main()
