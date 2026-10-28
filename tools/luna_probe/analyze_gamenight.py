import json

with open("tools/luna_probe/dumps/03b_game_night_alt.json", "r", encoding="utf-8") as f:
    data = json.load(f)["response"]

widgets = data.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
for w in widgets:
    wid = w.get("id")
    wtype = w.get("type")
    pstr = w.get("presentationData", "{}")
    p = json.loads(pstr) if isinstance(pstr, str) else pstr
    subw = w.get("widgets", [])
    print(f"\nWidget: {wid} ({wtype}) - title: {p.get('title')}")
    print(f"  subwidgets: {len(subw)}")
    for idx, sw in enumerate(subw[:2]):
        swp = json.loads(sw.get("presentationData", "{}"))
        print(f"    [{idx}] {sw.get('id')} ({sw.get('type')}) - title: {swp.get('title') or swp.get('gameTitle') or swp.get('primaryText')}")
