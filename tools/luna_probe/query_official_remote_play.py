import urllib.request
import json

with open("scratch/live_token.json") as f:
    token_data = json.load(f)
token = token_data.get("access_token")

headers = {
    "x-amz-platform": "windows",
    "x-amz-device-type": "A1PY8QQU9P0WJV",
    "x-amz-access-token": token,
    "x-amz-marketplace-id": "A1RKKUPIHCS9HS",
    "x-amz-locale": "es_ES",
    "Content-Type": "application/json"
}

payload = {
    "featureScheme": "",
    "serviceToken": "eyJlbmNyeXB0UGFnZUlkIjpmYWxzZSwicGFnZUlkIjoiZGVmYXVsdCIsInBhZ2VUeXBlIjoibXVsdGlzdGF0ZV9sYW5kaW5nX3JlbW90ZV9wbGF5In0="
}

req = urllib.request.Request("https://proxy-prod.eu-west-1.tempo.digital.a2z.com/getPage", data=json.dumps(payload).encode("utf-8"), headers=headers)
with urllib.request.urlopen(req) as resp:
    data = json.loads(resp.read().decode("utf-8"))
    with open("tools/luna_probe/dumps/05_official_remote_play.json", "w", encoding="utf-8") as out:
        json.dump({"response": data}, out, indent=2, ensure_ascii=False)
    print("Remote Play SUCCESS HTTP 200!")
    mg = data.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
    print(f"mainContent widgets: {len(mg)}")
    for w in mg:
        wid = w.get("id")
        wtype = w.get("type")
        pstr = w.get("presentationData", "{}")
        p = json.loads(pstr) if isinstance(pstr, str) else pstr
        print(f"  - {wid} ({wtype}) - pdata: {p}")
        subw = w.get("widgets", [])
        if subw:
            for sw in subw:
                swp_str = sw.get("presentationData", "{}")
                swp = json.loads(swp_str) if isinstance(swp_str, str) else swp_str
                print(f"      sub: {sw.get('id')} ({sw.get('type')}) - {swp}")
