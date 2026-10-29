import glob
import json
import os
import re

json_files = glob.glob('tools/luna_probe/captures/captura_pagina_1_inicio/api/*.json')

videos = {}
games = {}

for f in json_files:
    try:
        with open(f, encoding='utf-8') as fp:
            d = json.load(fp)
    except:
        continue

    # search in raw text or parsed
    raw = json.dumps(d)
    
    # Check for presentationData
    p_datas = re.findall(r'"presentationData":\s*"({.+?})"', raw)
    for pd_str in p_datas:
        try:
            # unescape
            clean_pd = pd_str.replace(r'\"', '"').replace(r'\\', '\\')
            pd = json.loads(clean_pd)
            title = pd.get('title')
            asin = pd.get('productId') or pd.get('asin')
            if title and asin:
                if asin not in games:
                    games[asin] = pd
            for k, v in pd.items():
                if isinstance(v, str) and ('.mp4' in v or '.webm' in v or '.m3u8' in v):
                    videos[v] = {'key': k, 'game': title, 'asin': asin}
        except Exception:
            pass

    # Direct search for any media-amazon mp4/webm/m3u8
    mp4s = re.findall(r'https?://[^\s"\'<>]+\.(?:mp4|webm|m3u8)', raw)
    for u in mp4s:
        if u not in videos:
            videos[u] = {'url': u, 'file': os.path.basename(f)}

print(f'Total Games parsed from presentationData: {len(games)}')
print(f'Total Videos found: {len(videos)}')
for u, v in list(videos.items())[:15]:
    print(f'  {u} -> {v}')
