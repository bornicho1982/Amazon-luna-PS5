#!/usr/bin/env python3
"""Lightweight ELF64 inventory for extracted PS5 SELF images.

The extracted ELFs have no section headers, but they keep program headers,
PT_DYNAMIC, dynamic symbols and .rodata strings. This tool parses those without
requiring pyelftools, and writes TSV/string inventories under reports/.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

DT_NEEDED = 1
DT_HASH = 4
DT_STRTAB = 5
DT_SYMTAB = 6
DT_STRSZ = 10
DT_SYMENT = 11
DT_SONAME = 14
DT_RPATH = 15
DT_RUNPATH = 29
DT_GNU_HASH = 0x6FFFFEF5
PT_LOAD = 1
PT_DYNAMIC = 2

STT = {
    0: "NOTYPE",
    1: "OBJECT",
    2: "FUNC",
    3: "SECTION",
    4: "FILE",
    5: "COMMON",
    6: "TLS",
    10: "GNU_IFUNC",
}


def read_cstr(data: bytes, offset: int, limit: int | None = None) -> str:
    if offset < 0 or offset >= len(data):
        return ""
    end = data.find(b"\0", offset)
    if end < 0:
        end = len(data)
    if limit is not None:
        end = min(end, offset + limit)
    return data[offset:end].decode("utf-8", "replace")


def parse_elf(path: Path) -> dict:
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise ValueError(f"{path}: not an ELF64 file")

    e_type, e_machine, e_version, e_entry, e_phoff, e_shoff = struct.unpack_from(
        "<HHIQQQ", data, 16
    )
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 54)

    phdrs = []
    for index in range(e_phnum):
        offset = e_phoff + index * e_phentsize
        values = struct.unpack_from("<IIQQQQQQ", data, offset)
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = values
        phdrs.append(
            {
                "index": index,
                "type": p_type,
                "flags": p_flags,
                "offset": p_offset,
                "vaddr": p_vaddr,
                "filesz": p_filesz,
                "memsz": p_memsz,
                "align": p_align,
            }
        )

    def vaddr_to_offset(vaddr: int) -> int | None:
        for ph in phdrs:
            if ph["type"] != PT_LOAD:
                continue
            if ph["vaddr"] <= vaddr < ph["vaddr"] + ph["filesz"]:
                return ph["offset"] + (vaddr - ph["vaddr"])
        return None

    dynamic = next((ph for ph in phdrs if ph["type"] == PT_DYNAMIC), None)
    tags: dict[int, list[int]] = {}
    if dynamic:
        offset = dynamic["offset"]
        while offset + 16 <= len(data):
            tag, value = struct.unpack_from("<QQ", data, offset)
            if tag == 0:
                break
            tags.setdefault(tag, []).append(value)
            offset += 16

    def tag_value(tag: int, default=None):
        values = tags.get(tag)
        return values[0] if values else default

    strtab_vaddr = tag_value(DT_STRTAB)
    strtab_size = tag_value(DT_STRSZ, 0)
    symtab_vaddr = tag_value(DT_SYMTAB)
    syment = tag_value(DT_SYMENT, 24)
    strtab_offset = vaddr_to_offset(strtab_vaddr) if strtab_vaddr is not None else None
    symtab_offset = vaddr_to_offset(symtab_vaddr) if symtab_vaddr is not None else None

    needed = []
    soname = ""
    runpath = []
    if strtab_offset is not None:
        for value in tags.get(DT_NEEDED, []):
            needed.append(read_cstr(data, strtab_offset + value))
        if DT_SONAME in tags:
            soname = read_cstr(data, strtab_offset + tags[DT_SONAME][0])
        for tag in (DT_RPATH, DT_RUNPATH):
            for value in tags.get(tag, []):
                runpath.append(read_cstr(data, strtab_offset + value))

    symbol_count = 0
    hash_vaddr = tag_value(DT_HASH)
    gnu_hash_vaddr = tag_value(DT_GNU_HASH)
    if hash_vaddr is not None:
        hash_offset = vaddr_to_offset(hash_vaddr)
        if hash_offset is not None:
            _, symbol_count = struct.unpack_from("<II", data, hash_offset)
    elif gnu_hash_vaddr is not None:
        gnu_offset = vaddr_to_offset(gnu_hash_vaddr)
        if gnu_offset is not None:
            nbuckets, symoffset, bloom_size, bloom_shift = struct.unpack_from(
                "<IIII", data, gnu_offset
            )
            buckets_offset = gnu_offset + 16 + bloom_size * 8
            buckets = struct.unpack_from(f"<{nbuckets}I", data, buckets_offset)
            chain_offset = buckets_offset + nbuckets * 4
            maximum = max(buckets) if buckets else 0
            if maximum >= symoffset:
                index = maximum
                while True:
                    chain = struct.unpack_from(
                        "<I", data, chain_offset + (index - symoffset) * 4
                    )[0]
                    if chain & 1:
                        break
                    index += 1
                symbol_count = index + 1

    symbols = []
    if symtab_offset is not None and strtab_offset is not None:
        if not symbol_count:
            # Conservative fallback: stop after a long run of invalid names.
            limit = max(0, (strtab_offset - symtab_offset) // syment)
            invalid = 0
            for index in range(limit):
                name_offset = struct.unpack_from("<I", data, symtab_offset + index * syment)[0]
                if name_offset >= strtab_size:
                    invalid += 1
                    if invalid > 16:
                        break
                else:
                    invalid = 0
                    symbol_count = index + 1
        for index in range(symbol_count):
            offset = symtab_offset + index * syment
            if offset + 24 > len(data):
                break
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from(
                "<IBBHQQ", data, offset
            )
            name = read_cstr(data, strtab_offset + st_name) if st_name < strtab_size else ""
            symbols.append(
                {
                    "index": index,
                    "value": st_value,
                    "size": st_size,
                    "info": st_info,
                    "other": st_other,
                    "shndx": st_shndx,
                    "name": name,
                }
            )

    return {
        "path": path,
        "data": data,
        "header": {
            "type": e_type,
            "machine": e_machine,
            "entry": e_entry,
            "phoff": e_phoff,
            "shoff": e_shoff,
            "phentsize": e_phentsize,
            "phnum": e_phnum,
        },
        "phdrs": phdrs,
        "tags": tags,
        "needed": needed,
        "soname": soname,
        "runpath": runpath,
        "symbols": symbols,
    }


def extract_strings(data: bytes, minimum: int = 6):
    offset = 0
    size = len(data)
    while offset < size:
        while offset < size and not (0x20 <= data[offset] <= 0x7E):
            offset += 1
        start = offset
        while offset < size and 0x20 <= data[offset] <= 0x7E:
            offset += 1
        if offset - start >= minimum:
            yield start, data[start:offset].decode("latin-1")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--min-string", type=int, default=6)
    parser.add_argument("--no-strings", action="store_true")
    args = parser.parse_args()

    reports = Path("reports")
    reports.mkdir(exist_ok=True)

    for path in args.files:
        info = parse_elf(path)
        data = info["data"]
        stem = path.stem
        print("=" * 78)
        print(f"{path}  size={len(data)}")
        print(
            "entry={entry:#x} phnum={phnum} type={type:#x} machine={machine:#x}".format(
                **info["header"]
            )
        )
        print(f"soname={info['soname']!r}")
        print(f"needed={info['needed']}")
        if info["runpath"]:
            print(f"runpath={info['runpath']}")
        print(f"dynamic_symbols={len(info['symbols'])}")

        functions = [s for s in info["symbols"] if (s["info"] & 0xF) == 2 and s["name"]]
        print(f"named_functions={len(functions)}")
        for symbol in functions[:20]:
            print(f"  FUNC {symbol['value']:#x} {symbol['name']}")

        tsv = reports / f"{stem}-dynsyms.tsv"
        with tsv.open("w", encoding="utf-8", errors="replace") as handle:
            handle.write("index\tvalue\tsize\ttype\tbind\tother\tshndx\tname\n")
            for symbol in info["symbols"]:
                kind = STT.get(symbol["info"] & 0xF, str(symbol["info"] & 0xF))
                bind = symbol["info"] >> 4
                handle.write(
                    f"{symbol['index']}\t{symbol['value']:#x}\t{symbol['size']}\t"
                    f"{kind}\t{bind}\t{symbol['other']:#x}\t{symbol['shndx']}\t"
                    f"{symbol['name']}\n"
                )
        print(f"wrote {tsv}")

        if not args.no_strings:
            strings = reports / f"{stem}-strings.txt"
            count = 0
            with strings.open("w", encoding="utf-8", errors="replace") as handle:
                for offset, text in extract_strings(data, args.min_string):
                    handle.write(f"{offset:08x}\t{text}\n")
                    count += 1
            print(f"wrote {strings} ({count} strings)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
