import json

with open("tools/luna_probe/dumps/real_detail_star_wars_racer.json", "r", encoding="utf-8") as f:
    data = json.load(f)["response"]

widgets = data.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
for w in widgets:
    wid = w.get("id")
    wtype = w.get("type")
    pstr = w.get("presentationData", "{}")
    pdata = json.loads(pstr) if isinstance(pstr, str) else pstr
    subw = w.get("widgets", [])
    print(f"\n==========================================")
    print(f"Widget: {wid} | Type: {wtype}")
    print(f"Presentation keys: {list(pdata.keys())}")
    for k in ["title", "description", "backgroundImage", "heroImage", "productLogo", "logoImage", "genres", "releaseDate", "developer", "publisher"]:
        if k in pdata:
            print(f"  {k}: {pdata[k]}")
    if subw:
        print(f"Subwidgets ({len(subw)}):")
        for idx, sw in enumerate(subw[:4]):
            sw_pstr = sw.get("presentationData", "{}")
            sw_p = json.loads(sw_pstr) if isinstance(sw_pstr, str) else sw_pstr
            print(f"  [{idx}] id={sw.get('id')} type={sw.get('type')}")
            print(f"       keys: {list(sw_p.keys())}")
            for pk in ["imageUrl", "highResImageUrl", "videoUrl", "title", "label", "value"]:
                if pk in sw_p:
                    print(f"       {pk}: {str(sw_p[pk])[:80]}")
