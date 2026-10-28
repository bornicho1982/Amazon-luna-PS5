import json
import os

dump_dir = "tools/luna_probe/dumps"
for fname in sorted(os.listdir(dump_dir)):
    fpath = os.path.join(dump_dir, fname)
    with open(fpath, "r", encoding="utf-8") as f:
        d = json.load(f)
    resp = d.get("response", {})
    print(f"=== {fname} ===")
    print("Keys in response:", list(resp.keys()))
    if "pageMemberGroups" in resp:
        for gname, gdata in resp["pageMemberGroups"].items():
            w_list = gdata.get("widgets", [])
            print(f"  Group '{gname}': {len(w_list)} widgets")
            for w in w_list:
                wid = w.get("id")
                wtype = w.get("type")
                wtitle = w.get("header", {}).get("title") or w.get("title")
                item_count = len(w.get("items", []))
                print(f"    - id={wid}, type={wtype}, items={item_count}, title={wtitle}")
    if "page" in resp:
        print("  'page':", list(resp["page"].keys()))
        if "widgets" in resp["page"]:
            print(f"  page.widgets: {len(resp['page']['widgets'])}")
    if "dialog" in resp:
        print("  'dialog':", resp["dialog"])
