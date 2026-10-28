import json

with open("tools/luna_probe/dumps/02_landing_home_serviceToken.json", "r", encoding="utf-8") as f:
    full = json.load(f)["response"]

print("Full response top-level keys:", list(full.keys()))
for k in ["actions", "presentationData", "pageMemberGroups"]:
    if k in full:
        print(f"Top-level key '{k}': type={type(full[k])}, len={len(full[k])}")

widgets = full.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
for w in widgets[:4]:
    print(f"\n--- Widget: {w.get('id')} ({w.get('type')}) ---")
    print("Widget keys:", list(w.keys()))
    for k, v in w.items():
        if k not in ["type", "id"]:
            print(f"  {k}: {type(v)} {str(v)[:150]}")
