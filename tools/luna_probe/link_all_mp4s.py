import glob
import json
import os
import shutil

VIDEOS_DIR = "PPSA99825/videos"

# Map md5 hash to downloaded files
files = glob.glob(f"{VIDEOS_DIR}/*.mp4")
hash_map = {}
for f in files:
    bn = os.path.basename(f)
    # Check if ends with _[hash].mp4
    m = re_match = None
    import re
    m = re.search(r'_([a-f0-9]{8})\.mp4$', bn)
    if m:
        hash_map[m.group(1)] = f

import hashlib

def link_url(u):
    raw_name = u.split('/')[-1]
    dst = os.path.join(VIDEOS_DIR, raw_name)
    if os.path.exists(dst): return
    u_hash = hashlib.md5(u.encode()).hexdigest()[:8]
    if u_hash in hash_map:
        shutil.copy2(hash_map[u_hash], dst)
        print(f"Linked {raw_name} -> {os.path.basename(hash_map[u_hash])}")

# Scan all json files in captures and dumps
for jf in glob.glob("tools/luna_probe/**/*.json", recursive=True):
    try:
        txt = open(jf, encoding='utf-8').read()
        urls = re.findall(r'https://m\.media-amazon\.com/images/I/[A-Za-z0-9+-]+\.mp4', txt)
        for u in set(urls):
            link_url(u)
    except: pass
