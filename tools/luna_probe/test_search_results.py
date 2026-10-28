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

payload = {
    "featureScheme": "",
    "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "multistate_search_results"},
    "searchContext": {"query": "Sonic", "sort": "TITLE_A_TO_Z"}
}

req = urllib.request.Request("https://proxy-prod.eu-west-1.tempo.digital.a2z.com/getPage", data=json.dumps(payload).encode("utf-8"), headers=headers)
try:
    with urllib.request.urlopen(req) as resp:
        d = json.loads(resp.read().decode("utf-8"))
        print("[+] multistate_search_results HTTP 200!")
        mg = d.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
        print(f"    mainContent widgets: {len(mg)}")
        for w in mg:
            sub = w.get("widgets", [])
            print(f"      - {w.get('id')} ({w.get('type')}) items: {len(sub)}")
            for sw in sub[:5]:
                swp = json.loads(sw.get("presentationData", "{}"))
                print(f"          sw: {swp.get('title')} ({swp.get('imageLandscape')})")
        with open("tools/luna_probe/dumps/06_search_results_sonic.json", "w", encoding="utf-8") as out:
            json.dump({"response": d}, out, indent=2, ensure_ascii=False)
except Exception as e:
    print(f"[-] Error: {e}")
