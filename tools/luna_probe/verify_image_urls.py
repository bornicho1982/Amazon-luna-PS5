"""Scan dumped Luna JSON files for image URLs, normalize webp -> jpg and verify they download as JPEG/PNG."""
import glob
import json
import re
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor

DUMPS = glob.glob("tools/luna_probe/dumps/*.json")
URL_RE = re.compile(r'https?://[^"\s\\]+?\.(?:jpg|jpeg|png|webp)(?:\?[^"\s\\]*)?', re.I)


def sanitize(url: str) -> str:
    # Mirror of sanitizeAmazonImageUrl() in C++
    u = url.replace("_FMwebp_", "_FMjpg_")
    u = re.sub(r"\.webp(?=$|\?)", ".jpg", u, flags=re.I)
    return u


def magic(b: bytes) -> str:
    if b[:3] == b"\xff\xd8\xff":
        return "JPEG"
    if b[:8] == b"\x89PNG\r\n\x1a\n":
        return "PNG"
    if b[:4] == b"RIFF" and b[8:12] == b"WEBP":
        return "WEBP"
    return "UNKNOWN"


def fetch(url):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0", "Accept": "image/jpeg,image/png,*/*"})
        with urllib.request.urlopen(req, timeout=15) as r:
            body = r.read(64)
            return url, r.status, magic(body)
    except Exception as e:  # noqa
        return url, 0, str(e)[:60]


def main():
    urls = {}
    for f in DUMPS:
        txt = open(f, "r", encoding="utf-8", errors="ignore").read()
        txt = txt.replace("\\/", "/")
        for m in URL_RE.findall(txt):
            urls.setdefault(m, set()).add(f.split("\\")[-1].split("/")[-1])
    print(f"files={len(DUMPS)} unique_urls={len(urls)}")
    webp = [u for u in urls if ".webp" in u.lower() or "_FMwebp_" in u]
    print(f"webp-ish urls: {len(webp)}")
    for u in webp[:5]:
        print("  webp:", u, "->", sanitize(u))

    sample = list(urls)[:150]
    results = []
    with ThreadPoolExecutor(12) as ex:
        for r in ex.map(lambda u: fetch(sanitize(u)), sample):
            results.append(r)
    ok = sum(1 for _, s, m in results if s == 200 and m in ("JPEG", "PNG"))
    print(f"checked={len(results)} ok(JPEG/PNG)={ok}")
    for u, s, m in results:
        if not (s == 200 and m in ("JPEG", "PNG")):
            print("  BAD:", s, m, u)

    # detail JSON checks
    for f in sorted(glob.glob("tools/luna_probe/dumps/real_detail_*.json")):
        txt = open(f, "r", encoding="utf-8", errors="ignore").read().replace("\\/", "/")
        found = set(URL_RE.findall(txt))
        shots = [u for u in found if re.search(r"screenshot|_ss|trailer|ScreenShot", u, re.I)]
        print(f"{f.split(chr(92))[-1]}: urls={len(found)} screenshot-like={len(shots)}")


if __name__ == "__main__":
    main()
