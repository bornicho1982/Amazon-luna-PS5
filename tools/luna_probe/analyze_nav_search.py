import json

with open("tools/luna_probe/dumps/01_navigation.json", "r", encoding="utf-8") as f:
    d = json.load(f)["response"]

for gname, gdata in d.get("pageMemberGroups", {}).items():
    for w in gdata.get("widgets", []):
        if "search" in w.get("id", ""):
            print(f"Widget {w.get('id')} in group {gname}:")
            print("  Presentation:", w.get("presentationData"))
            print("  Actions:", w.get("actions"))
