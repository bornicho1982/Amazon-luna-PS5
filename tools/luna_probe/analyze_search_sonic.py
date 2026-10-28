import json

with open("tools/luna_probe/dumps/06_official_search_sonic.json", "r", encoding="utf-8") as f:
    d = json.load(f)["response"]

print("Search response keys:", list(d.keys()))
for g, v in d.get("pageMemberGroups", {}).items():
    print(f"Group: {g}")
    for w in v.get("widgets", []):
        print(f"  Widget {w.get('id')} ({w.get('type')}) - subwidgets: {len(w.get('widgets', []))}")
