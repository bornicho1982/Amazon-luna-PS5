import os
import sys
import json
import urllib.request
import urllib.error

TOKEN_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../scratch/live_token.json"))
with open(TOKEN_PATH, "r", encoding="utf-8") as f:
    token = json.load(f)["access_token"]

def query_game(product_id, name):
    url = "https://proxy-prod.eu-west-1.tempo.digital.a2z.com/getPage"
    headers = {
        "x-amz-platform": "windows",
        "x-amz-device-type": "A1PY8QQU9P0WJV",
        "x-amz-access-token": token,
        "x-amz-marketplace-id": "A1RKKUPIHCS9HS",
        "x-amz-locale": "es_ES",
        "Accept-Language": "es-ES,es;q=0.9,en;q=0.8",
        "Content-Type": "application/json"
    }
    payload = {
        "featureScheme": "",
        "pageContext": {
            "encryptPageId": False,
            "pageId": product_id,
            "pageType": "game_detail",
            "productStage": "Release"
        }
    }
    req = urllib.request.Request(url, data=json.dumps(payload).encode("utf-8"), headers=headers)
    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            fname = f"tools/luna_probe/dumps/real_detail_{name}.json"
            with open(fname, "w", encoding="utf-8") as out:
                json.dump({"response": data}, out, indent=2, ensure_ascii=False)
            print(f"[+] Saved {name}: status 200, groups: {list(data.get('pageMemberGroups', {}).keys())}")
            mg = data.get("pageMemberGroups", {}).get("mainContent", {}).get("widgets", [])
            print(f"    mainContent widgets ({len(mg)}):")
            for w in mg:
                print(f"      - {w.get('id')} ({w.get('type')})")
    except Exception as e:
        print(f"[-] Error querying {name}: {e}")

query_game("amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16", "star_wars_racer")
query_game("amzn1.luna.product.588cc88e-de1f-427b-97ba-7b2e42846894", "bluey")
query_game("amzn1.adg.product.9b5d2e6f-0824-4e41-a481-def995e5098a", "hogwarts_legacy")
query_game("amzn1.adg.product.7bd9679c-c3ed-4edf-8ac1-81d6a2cd381a", "tomb_raider")
