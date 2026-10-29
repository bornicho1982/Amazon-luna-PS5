import glob
import os
import re

html_files = glob.glob('tools/luna_probe/captures/captura_pagina_1_inicio/*.html')
for f in html_files:
    txt = open(f, encoding='utf-8').read()
    v_tags = re.findall(r'<video[^>]*>', txt, re.I)
    v_urls = re.findall(r'https?://[^\s"\'<>]+\.(?:mp4|webm|m3u8)', txt, re.I)
    print(f, 'video tags:', len(v_tags), 'video urls:', len(v_urls))
    for t in v_tags:
        print('  TAG:', t[:150])
    for u in set(v_urls):
        print('  URL:', u)

print('\nSearching in API JSON files...')
json_files = glob.glob('tools/luna_probe/captures/captura_pagina_1_inicio/api/*.json')
found_in_json = set()
for jf in json_files:
    try:
        raw = open(jf, encoding='utf-8').read()
        urls = re.findall(r'https?://[^\s"\'<>]+\.(?:mp4|webm|m3u8)', raw, re.I)
        for u in urls:
            found_in_json.add((u, jf))
        # Also check for "videoUrl" or "trailer" or "stream"
        v_keys = re.findall(r'"[^"]*(?:video|trailer|media)[^"]*":\s*"([^"]+)"', raw, re.I)
        for vk in v_keys:
            if vk.startswith('http'):
                found_in_json.add((vk, jf))
    except Exception:
        pass

print(f'Total video matches in JSON: {len(found_in_json)}')
for u, jf in list(found_in_json)[:20]:
    print(f'  {u} (in {os.path.basename(jf)})')
