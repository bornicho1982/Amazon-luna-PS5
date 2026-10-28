import json

with open("tools/luna_probe/dumps/08b_settings.json", "r", encoding="utf-8") as f:
    data = json.load(f)["response"]

widgets = data.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
for w in widgets:
    wid = w.get("id")
    wtype = w.get("type")
    subw = w.get("widgets", [])
    print(f"Settings widget: {wid} ({wtype}), subwidgets: {len(subw)}")
    for idx, sw in enumerate(subw):
        pstr = sw.get("presentationData", "{}")
        p = json.loads(pstr) if isinstance(pstr, str) else pstr
        print(f"  [{idx}] {sw.get('id')} ({sw.get('type')}) - title: {p.get('title') or p.get('primaryText')} target: {sw.get('actions', [{}])[0].get('target')}")
