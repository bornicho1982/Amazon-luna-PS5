#!/usr/bin/env python3
"""Extract PS5 SELF/SPRX files on-console and download the resulting ELF.

Usage:
  python tools/extract_self.py \
      --remote /system_ex/common_ex/lib/libSceNKWebKit.sprx \
      --elf-name libSceNKWebKit.elf

The payload source is in payloads/src/selfextract. It uses the public ftpsrv
SELF pager code. The extracted ELF is written to /data/ps5lab on the console
and downloaded through the PS5 Web File Manager on port 8888.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = ROOT / "payloads" / "src" / "selfextract"
PAYLOAD = ROOT / "payloads" / "selfextract.elf"
REPORTS = ROOT / "reports"
DUMPS = ROOT / "dumps"
SDK = Path(r"C:\ps5-payload-sdk")
BUILD = SDK / "win" / "prospero-clang-ps5lab.cmd"
LLVM_BIN = Path(r"C:\llvm-ps5\bin")

DEFAULT_HOST = "192.168.18.21"
LOADER_PORT = 9021
WEB_PORT = 8888


def write_target(remote: str, elf_name: str) -> None:
    target = SRC_DIR / "target.h"
    text = (
        "#pragma once\n\n"
        f'#define SELF_PATH "{remote}"\n'
        f'#define ELF_PATH  "/data/ps5lab/{elf_name}"\n'
    )
    target.write_text(text, encoding="utf-8")
    print(f"target: {remote} -> /data/ps5lab/{elf_name}")


def build_payload() -> None:
    env = os.environ.copy()
    env["PATH"] = str(LLVM_BIN) + os.pathsep + env.get("PATH", "")
    command = [
        str(BUILD),
        "-Wall",
        "-Werror",
        "-g",
        "-o",
        "selfextract.elf",
        "main.c",
        "self-prospero.c",
    ]
    print("build:", " ".join(command))
    completed = subprocess.run(
        command,
        cwd=str(SRC_DIR),
        env=env,
        capture_output=True,
        text=True,
        timeout=120,
    )
    if completed.returncode != 0:
        print(completed.stdout)
        print(completed.stderr, file=sys.stderr)
        raise SystemExit(f"build failed ({completed.returncode})")
    shutil.copy2(SRC_DIR / "selfextract.elf", PAYLOAD)
    print(f"payload: {PAYLOAD} ({PAYLOAD.stat().st_size} bytes)")


def send_payload(host: str, timeout: float = 300.0) -> str:
    sys.path.insert(0, str(ROOT))
    from ps5lab.net import send_file

    result = send_file(
        host,
        LOADER_PORT,
        str(PAYLOAD),
        timeout=timeout,
        read_timeout=timeout,
        max_response=2_000_000,
        max_text=2_000_000,
    )
    text = result.get("response_text", "")
    if result.get("error"):
        print("loader error:", result["error"], file=sys.stderr)
    print(text)
    return text


def download_elf(host: str, elf_name: str) -> Path:
    remote = f"/data/ps5lab/{elf_name}"
    url = f"http://{host}:{WEB_PORT}/fs?path=" + urllib.parse.quote(remote, safe="")
    dest = DUMPS / elf_name
    dest.parent.mkdir(parents=True, exist_ok=True)
    print("download:", url)
    digest = hashlib.sha256()
    total = 0
    started = time.time()
    req = urllib.request.Request(url, headers={"Accept-Encoding": "identity"})
    with urllib.request.urlopen(req, timeout=60) as response, open(dest, "wb") as f:
        expected = int(response.headers.get("Content-Length", "0"))
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            f.write(chunk)
            digest.update(chunk)
            total += len(chunk)
    elapsed = time.time() - started
    print(
        f"downloaded {elf_name}: {total} bytes "
        f"(expected {expected}) in {elapsed:.2f}s"
    )
    print(f"sha256: {digest.hexdigest()}")
    return dest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--remote", required=True, help="path on the PS5")
    parser.add_argument("--elf-name", required=True, help="output ELF name")
    parser.add_argument("--no-download", action="store_true")
    args = parser.parse_args()

    REPORTS.mkdir(exist_ok=True)
    DUMPS.mkdir(exist_ok=True)

    write_target(args.remote, args.elf_name)
    build_payload()
    text = send_payload(args.host)
    report = REPORTS / f"selfextract-{Path(args.elf_name).stem}-2026-10-20.txt"
    report.write_text(text, encoding="utf-8", errors="replace")
    print(f"report: {report}")
    if "PS5LAB_SELF_EXTRACT_OK" not in text:
        print("extraction did not report success", file=sys.stderr)
        return 1
    if not args.no_download:
        download_elf(args.host, args.elf_name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
