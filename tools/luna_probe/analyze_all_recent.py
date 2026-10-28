import json

with open("tools/luna_probe/dumps/04_landing_library.json", "r", encoding="utf-8") as f:
    data = json.load(f)["response"]

subw = data.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])[0].get("widgets", [])
for idx, sw in enumerate(subw):
    p = json.loads(sw.get("presentationData", "{}"))
    print(f"[{idx}] {p.get('gameTitle')} | Date: '{p.get('lastPlayedDate')}' | Img: {p.get('tileImageLandscape')}")
