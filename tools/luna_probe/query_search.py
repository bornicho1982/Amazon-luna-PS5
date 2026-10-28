import urllib.request
import json

with open("scratch/live_token.json") as f:
    token = json.load(f)["access_token"]

headers = {
    "x-amz-platform": "windows",
    "x-amz-device-type": "A1PY8QQU9P0WJV",
    "x-amz-access-token": token,
    "x-amz-marketplace-id": "A1RKKUPIHCS9HS",
    "x-amz-locale": "es_ES",
    "Content-Type": "application/json"
}

# Test 1: landing_search with searchContext
p1 = {
    "featureScheme": "",
    "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "landing_search"},
    "searchContext": {"query": "Sonic", "sort": "TITLE_A_TO_Z"}
}

req1 = urllib.request.Request("https://proxy-prod.eu-west-1.tempo.digital.a2z.com/getPage", data=json.dumps(p1).encode("utf-8"), headers=headers)
try:
    with urllib.request.urlopen(req1) as resp:
        d = json.loads(resp.read().decode("utf-8"))
        print("[+] landing_search SUCCESS HTTP 200!")
        mg = d.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
        print(f"    widgets ({len(mg)}):")
        for w in mg:
            print(f"      - {w.get('id')} ({w.get('type')}) - subwidgets: {len(w.get('widgets', []))}")
        with open("tools/luna_probe/dumps/06_official_search_sonic.json", "w", encoding="utf-8") as out:
            json.dump({"response": d}, out, indent=2, ensure_ascii=False)
except Exception as e:
    print(f"[-] landing_search error: {e}")
