import json

with open("tools/luna_probe/dumps/04_landing_library.json", "r", encoding="utf-8") as f:
    data = json.load(f)["response"]

widgets = data.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
w0 = widgets[0]
subw = w0.get("widgets", [])
if subw:
    sw0 = subw[0]
    print("Keys of sw0:", list(sw0.keys()))
    print("sw0 full:", json.dumps(sw0, indent=2))
