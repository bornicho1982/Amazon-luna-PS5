import glob
import hashlib
import os
import shutil

VIDEOS_SRC = "tools/luna_probe/captures/captura_pagina_1_inicio/assets/videos"
DEST = "PPSA99825/videos"
os.makedirs(DEST, exist_ok=True)

# Also create mapping from URL to local file
import json
bp_file = "tools/luna_probe/captures/captura_pagina_1_inicio/inicio_blueprint.json"
url_to_file = {}
if os.path.exists(bp_file):
    bp = json.load(open(bp_file, encoding='utf-8'))
    for s in bp.get('sections', []):
        for it in s.get('items', []):
            u = it.get('video_url')
            fn = it.get('video_file')
            if u and fn:
                url_to_file[u] = fn

# Copy all videos to PPSA99825/videos
copied = 0
for src in glob.glob(f"{VIDEOS_SRC}/*.mp4"):
    bn = os.path.basename(src)
    dst = os.path.join(DEST, bn)
    if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src):
        shutil.copy2(src, dst)
        copied += 1

# Also create aliases using the URL's basename: e.g. F1PacPlgAmL.mp4
alias_count = 0
for u, fn in url_to_file.items():
    raw_name = u.split('/')[-1]
    src_file = os.path.join(DEST, fn)
    alias_file = os.path.join(DEST, raw_name)
    if os.path.exists(src_file) and not os.path.exists(alias_file):
        try:
            # create hardlink or copy
            shutil.copy2(src_file, alias_file)
            alias_count += 1
        except: pass

print(f"[+] Total videos copied to {DEST}: {copied}")
print(f"[+] Total direct URL filename aliases created: {alias_count}")
