import json

with open("tools/luna_probe/dumps/02_landing_home_serviceToken.json", "r", encoding="utf-8") as f:
    full = json.load(f)["response"]

widgets = full.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
for w in widgets:
    wid = w.get("id")
    wtype = w.get("type")
    pdata_str = w.get("presentationData", "{}")
    pdata = json.loads(pdata_str) if isinstance(pdata_str, str) else pdata_str
    sub_widgets = w.get("widgets", [])
    print(f"\n==========================================")
    print(f"Widget ID: {wid} | Type: {wtype}")
    print(f"Presentation: {pdata}")
    print(f"Subwidgets count: {len(sub_widgets)}")

    for idx, sw in enumerate(sub_widgets[:3]):
        swid = sw.get("id")
        swtype = sw.get("type")
        swp_str = sw.get("presentationData", "{}")
        swp = json.loads(swp_str) if isinstance(swp_str, str) else swp_str
        actions = sw.get("actions", [])
        print(f"  [{idx}] SubID: {swid} | Type: {swtype}")
        print(f"       Presentation keys: {list(swp.keys())}")
        if "title" in swp:
            print(f"       Title: {swp.get('title')}")
        if "badge" in swp:
            print(f"       Badge: {swp.get('badge')}")
        for img_key in ["focusedImageLandscape", "unfocusedImageLandscape", "imageLandscape", "backgroundImage", "heroImage", "productLogo", "logoImage", "image"]:
            if img_key in swp:
                print(f"       {img_key}: {swp[img_key][:80]}")
        for act in actions:
            target = act.get("target")
            payload = act.get("payload", {})
            print(f"       Action: target={target}, payload={payload}")
