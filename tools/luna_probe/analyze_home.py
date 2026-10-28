import json

with open("tools/luna_probe/dumps/02_landing_home_serviceToken.json", "r", encoding="utf-8") as f:
    d = json.load(f)["response"]

widgets = d.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
print(f"Total widgets: {len(widgets)}")
for w in widgets:
    wid = w.get("id")
    wtype = w.get("type")
    header = w.get("header", {})
    wtitle = header.get("title") or w.get("title")
    items = w.get("items", [])
    print(f"\n==========================================")
    print(f"Widget ID: {wid}")
    print(f"Widget Type: {wtype}")
    print(f"Widget Title: {wtitle}")
    print(f"Item Count: {len(items)}")

    # Check widget properties
    for k in ["backgroundImage", "button", "description", "icon", "upsellDetails"]:
        if k in w:
            print(f"  {k}: {w[k]}")

    for idx, item in enumerate(items[:3]):
        ititle = item.get("title") or item.get("header", {}).get("title")
        iid = item.get("id")
        action = item.get("primaryAction") or item.get("action") or {}
        st = action.get("serviceToken")
        pc = action.get("pageContext")
        bg = item.get("backgroundImage") or item.get("focusedImageLandscape") or item.get("imageLandscape")
        logo = item.get("logoImage") or item.get("productLogo") or item.get("titleImage")
        print(f"  [{idx}] title: '{ititle}', id: {iid}")
        print(f"      action target: {action.get('target')}")
        if pc:
            print(f"      pageContext: {pc}")
        if bg:
            print(f"      bg/landscape image: {bg}")
        if logo:
            print(f"      logo: {logo}")
