#!/usr/bin/env python3
"""Find code references to selected strings in a large ELF64 binary.

Ghidra gives the best results, but on a 60 MB WebKit library full analysis can
take a long time. This tool performs a fast linear disassembly of executable
PT_LOAD segments with Capstone and reports RIP-relative/absolute references to
the virtual addresses of selected strings.

It is a triage tool: hits must be validated in Ghidra or with a focused
disassembly, because a linear sweep can decode data as instructions.
"""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from elf_report import parse_elf  # noqa: E402


def load_string_offsets(path: Path) -> list[tuple[int, str]]:
    rows: list[tuple[int, str]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        offset, _, text = line.partition("\t")
        try:
            rows.append((int(offset, 16), text))
        except ValueError:
            continue
    return rows


def find_targets(rows: list[tuple[int, str]], queries: list[str]) -> dict[str, list[tuple[int, str]]]:
    result: dict[str, list[tuple[int, str]]] = {}
    for query in queries:
        matches = [(offset, text) for offset, text in rows if text == query]
        if not matches:
            matches = [(offset, text) for offset, text in rows if query in text]
        result[query] = matches
    return result


def offset_to_vaddr(phdrs: list[dict], offset: int) -> int | None:
    for ph in phdrs:
        if ph["type"] == 1 and ph["offset"] <= offset < ph["offset"] + ph["filesz"]:
            return ph["vaddr"] + (offset - ph["offset"])
    return None


def describe(insn) -> str:
    return f"{insn.address:#x}: {insn.mnemonic} {insn.op_str}".rstrip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--strings", type=Path, required=True)
    parser.add_argument("--target", action="append", default=[])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--max-hits", type=int, default=200)
    args = parser.parse_args()

    queries = args.target or [
        "WebProcess_IsJITEnabled",
        "WebProcess_IsJITEnabledReply",
        "/app0/WebProcess.self",
        "/app0/NetworkProcess.self",
        "FMEMSizeLimit",
        "SandboxExtension",
    ]

    info = parse_elf(args.elf)
    rows = load_string_offsets(args.strings)
    targets = find_targets(rows, queries)
    target_vaddrs: dict[int, tuple[str, str]] = {}
    print(f"ELF: {args.elf} size={len(info['data'])}")
    for query, matches in targets.items():
        if not matches:
            print(f"  target not found in strings: {query}")
            continue
        for offset, text in matches[:4]:
            vaddr = offset_to_vaddr(info["phdrs"], offset)
            if vaddr is None:
                print(f"  no PT_LOAD mapping for {query} at file offset {offset:#x}")
                continue
            target_vaddrs[vaddr] = (query, text)
            print(f"  target {query!r} file={offset:#x} vaddr={vaddr:#x} text={text[:110]!r}")

    if not target_vaddrs:
        print("No target virtual addresses; nothing to scan.")
        return 1

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    hits: list[tuple[str, int, str]] = []
    printed_context: set[int] = set()

    for ph in info["phdrs"]:
        if ph["type"] != 1 or not (ph["flags"] & 1) or ph["filesz"] == 0:
            continue
        code = info["data"][ph["offset"] : ph["offset"] + ph["filesz"]]
        print(f"scanning executable segment {ph['index']}: vaddr={ph['vaddr']:#x} size={len(code)}")
        recent: deque = deque(maxlen=10)
        after = 0
        for insn in md.disasm(code, ph["vaddr"]):
            hit: tuple[str, str] | None = None
            for op in insn.operands:
                addresses: list[int] = []
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    addresses.append(insn.address + insn.size + op.mem.disp)
                elif op.type == X86_OP_IMM:
                    addresses.append(op.imm & 0xFFFFFFFFFFFFFFFF)
                for address in addresses:
                    if address in target_vaddrs:
                        hit = target_vaddrs[address]
                        break
                if hit:
                    break

            if hit:
                query, text = hit
                hits.append((query, insn.address, describe(insn)))
                print(f"\nHIT {query!r} at {insn.address:#x}: {insn.mnemonic} {insn.op_str}")
                if insn.address not in printed_context:
                    for previous in list(recent)[-6:]:
                        print(f"  ctx {previous}")
                    printed_context.add(insn.address)
                after = 10

            if after > 0:
                if not hit:
                    print(f"  ctx {describe(insn)}")
                after -= 1

            recent.append(describe(insn))
            if len(hits) >= args.max_hits:
                print(f"\nReached max hits ({args.max_hits}); stopping.")
                break
        if len(hits) >= args.max_hits:
            break

    output = args.output or Path("reports") / f"string-xrefs-{args.elf.stem}.txt"
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as handle:
        handle.write(f"ELF: {args.elf}\n")
        handle.write(f"Strings: {args.strings}\n")
        handle.write("Targets:\n")
        for vaddr, (query, text) in sorted(target_vaddrs.items()):
            handle.write(f"  {vaddr:#x}\t{query}\t{text[:200]}\n")
        handle.write(f"\nHits: {len(hits)}\n")
        for query, address, description in hits:
            handle.write(f"{query}\t{address:#x}\t{description}\n")
    print(f"\n{len(hits)} hits; wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
