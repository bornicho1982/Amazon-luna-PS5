#!/usr/bin/env python3
import sys
import struct
import hashlib
import base64
import os
import glob

def nid(name):
    suffix = bytes([0x51, 0x8d, 0x64, 0xa6, 0x35, 0xde, 0xd8, 0xc1, 0xe6, 0xb0, 0x39, 0xb1, 0xc3, 0xe5, 0x52, 0x30])
    h = hashlib.sha1(name.encode('ascii') + suffix).digest()
    val = h[:8][::-1]
    b64 = base64.b64encode(val).decode('ascii')[:11]
    return b64.replace('/', '-')

def get_elf_symbols(path, undefined_only=False):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'\x7fELF':
        return []
    e_shoff = struct.unpack('<Q', data[0x28:0x30])[0]
    e_shentsize = struct.unpack('<H', data[0x3a:0x3c])[0]
    e_shnum = struct.unpack('<H', data[0x3c:0x3e])[0]
    e_shstrndx = struct.unpack('<H', data[0x3e:0x40])[0]
    sections = []
    for i in range(e_shnum):
        sh = data[e_shoff + i*e_shentsize : e_shoff + (i+1)*e_shentsize]
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = struct.unpack('<IIQQQQIIQQ', sh)
        sections.append({'name_idx': sh_name, 'type': sh_type, 'offset': sh_offset, 'size': sh_size, 'link': sh_link, 'entsize': sh_entsize})
    shstr = data[sections[e_shstrndx]['offset'] : sections[e_shstrndx]['offset'] + sections[e_shstrndx]['size']]
    for s in sections:
        end = shstr.find(b'\0', s['name_idx'])
        s['name'] = shstr[s['name_idx']:end].decode('ascii', errors='ignore')

    dynsym_sec = next((s for s in sections if s['type'] == 11), None)
    if not dynsym_sec:
        return []
    dynstr_sec = sections[dynsym_sec['link']]
    dynstr = data[dynstr_sec['offset'] : dynstr_sec['offset'] + dynstr_sec['size']]

    syms = []
    for i in range(dynsym_sec['size'] // dynsym_sec['entsize']):
        sym = data[dynsym_sec['offset'] + i*24 : dynsym_sec['offset'] + (i+1)*24]
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack('<IBBHQQ', sym)
        if st_name == 0:
            continue
        end = dynstr.find(b'\0', st_name)
        name = dynstr[st_name:end].decode('ascii', errors='ignore')
        bind = st_info >> 4
        if undefined_only:
            if st_shndx == 0 and name:
                syms.append(name)
        else:
            if st_shndx != 0 and (bind in (1, 2)) and name:
                syms.append(name)
    return syms

def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    pie_path = os.path.join(root, 'apps', 'luna-ps5', 'ps5', 'obj', 'llvm-pie.elf')
    imports_txt = os.path.join(root, 'apps', 'luna-ps5', 'ps5', 'tooling', 'imports.txt')
    api_txt = os.path.join(root, 'apps', 'luna-ps5', 'ps5', 'tooling', 'api-surface.txt')

    if not os.path.exists(pie_path):
        print(f'ERROR: {pie_path} does not exist.')
        sys.exit(1)

    # 1. Load manifests
    with open(imports_txt) as f:
        known_imports = set(line.strip().split('|')[0] for line in f if line.strip() and not line.startswith('#'))

    with open(api_txt) as f:
        known_api_nids = set(line.strip().split('|')[0] for line in f if line.strip() and not line.startswith('#'))

    # 2. Load SDK stubs
    sdk_stubs_dir = 'C:/ps5-payload-sdk/target/lib'
    allowed_stubs = [
        'libSceAudioOut.so', 'libSceHttp.so', 'libSceNet.so', 'libScePad.so',
        'libSceSsl.so', 'libSceSysmodule.so', 'libSceSystemService.so', 'libSceUserService.so',
        'libkernel.so', 'libSceLibcInternal.so'
    ]
    custom_stubs = [
        os.path.join(root, 'apps', 'luna-ps5', 'ps5', 'stubs', 'libSceVideodec2.so'),
        os.path.join(root, 'apps', 'luna-ps5', 'ps5', 'stubs', 'libSceVideoOut.so')
    ]

    stub_exports = set()
    for stub in allowed_stubs:
        full = os.path.join(sdk_stubs_dir, stub)
        if os.path.exists(full):
            stub_exports.update(get_elf_symbols(full, undefined_only=False))

    for stub in custom_stubs:
        if os.path.exists(stub):
            stub_exports.update(get_elf_symbols(stub, undefined_only=False))

    # 3. Check undefined dynamic symbols in llvm-pie.elf
    und_symbols = get_elf_symbols(pie_path, undefined_only=True)
    print(f'Auditing {len(und_symbols)} dynamic undefined symbols in llvm-pie.elf...')

    prohibited = {'_ioctl', 'ioctl', 'kernel_mprotect', 'sceNetGetsockname'}
    found_prohibited = [s for s in und_symbols if s in prohibited]
    if found_prohibited:
        print(f'CRITICAL ERROR: Prohibited symbol(s) detected: {found_prohibited}')
        sys.exit(1)

    orphans = []
    for sym in und_symbols:
        sym_nid = nid(sym)
        in_imports = sym in known_imports
        in_api = sym_nid in known_api_nids
        in_stubs = sym in stub_exports
        if not (in_imports or in_api or in_stubs):
            orphans.append(sym)

    if orphans:
        print(f'CRITICAL ERROR: {len(orphans)} unmapped dynamic symbols found:')
        for o in orphans:
            print(f'  - {o}')
        sys.exit(1)

    print('OK: All dynamic symbols resolved successfully through imports.txt, libc.prx or official SDK stubs.')
    print('OK: Zero forbidden symbols (_ioctl, ioctl, kernel_mprotect, sceNetGetsockname) present.')

    # 3b. Verify eboot.elf module table (no libkernel_web or libkernel_sys)
    eboot_elf = os.path.join(root, 'apps', 'luna-ps5', 'ps5', 'obj', 'eboot.elf')
    with open(eboot_elf, 'rb') as f:
        edata = f.read()
    if b'libkernel_web' in edata or b'libkernel_sys' in edata:
        print('CRITICAL ERROR: Forbidden module libkernel_web or libkernel_sys found in eboot.elf!')
        sys.exit(1)
    if b'libkernel.prx' not in edata:
        print('CRITICAL ERROR: Standard libkernel.prx missing from eboot.elf!')
        sys.exit(1)
    print('OK: eboot.elf links exclusively against standard libkernel.prx (0 references to libkernel_web).')

    # 4. Check signed outputs
    eboot_bin = os.path.join(root, 'PPSA99825', 'eboot.bin')
    libc_prx = os.path.join(root, 'PPSA99825', 'sce_module', 'libc.prx')
    for path, name in [(eboot_bin, 'eboot.bin'), (libc_prx, 'libc.prx')]:
        if not os.path.exists(path) or os.path.getsize(path) < 1000:
            print(f'CRITICAL ERROR: Missing signed container {name}')
            sys.exit(1)
        with open(path, 'rb') as f:
            magic = struct.unpack('<I', f.read(4))[0]
        if magic != 0x1D3D154F:
            print(f'CRITICAL ERROR: Invalid SELF magic in {name}: {hex(magic)}')
            sys.exit(1)
        print(f'OK: {name} valid FSELF (size: {os.path.getsize(path)} bytes).')

    sys.exit(0)

if __name__ == '__main__':
    main()
