import json

with open("tools/luna_probe/dumps/04_landing_library.json", "r", encoding="utf-8") as f:
    data = json.load(f)["response"]

widgets = data.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
for w in widgets:
    wid = w.get("id")
    wtype = w.get("type")
    pstr = w.get("presentationData", "{}")
    pdata = json.loads(pstr) if isinstance(pstr, str) else pstr
    subw = w.get("widgets", [])
    print(f"\n==========================================")
    print(f"Library Widget: {wid} | Type: {wtype} | Title: {pdata.get('title')}")
    print(f"Subwidgets count: {len(subw)}")
    for idx, sw in enumerate(subw[:6]):
        sw_pstr = sw.get("presentationData", "{}")
        sw_p = json.loads(sw_pstr) if isinstance(sw_pstr, str) else sw_pstr
        print(f"  [{idx}] {sw.get('id')} ({sw.get('type')}) - title: '{sw_p.get('title')}'")
        print(f"       lastPlayedAt: '{sw_p.get('lastPlayedAt')}'")
        print(f"       imageLandscape: '{sw_p.get('imageLandscape')}'")
        print(f"       imagePortrait: '{sw_p.get('imagePortrait')}'")
