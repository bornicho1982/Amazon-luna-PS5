import json

with open("tools/luna_probe/dumps/01_navigation.json", "r", encoding="utf-8") as f:
    data = json.load(f)["response"]

groups = data.get("pageMemberGroups", {})
for gname, gdata in groups.items():
    print(f"\nGroup: {gname}")
    for w in gdata.get("widgets", []):
        wid = w.get("id")
        wtype = w.get("type")
        pstr = w.get("presentationData", "{}")
        p = json.loads(pstr) if isinstance(pstr, str) else pstr
        print(f"  Widget {wid} ({wtype}) keys: {list(p.keys())}")
        if "avatarUrl" in p or "userName" in p or "gamerTag" in p or "title" in p or "primaryText" in p:
            print(f"    pdata: {p}")
        subw = w.get("widgets", [])
        if subw:
            print(f"    Subwidgets ({len(subw)}):")
            for sw in subw:
                swp_str = sw.get("presentationData", "{}")
                swp = json.loads(swp_str) if isinstance(swp_str, str) else swp_str
                print(f"      {sw.get('id')} ({sw.get('type')}) - {swp}")
