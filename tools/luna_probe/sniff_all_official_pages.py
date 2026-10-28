import os
import sys
import json
import time
import urllib.request
import urllib.error

DUMP_DIR = os.path.join(os.path.dirname(__file__), "dumps")
os.makedirs(DUMP_DIR, exist_ok=True)

TOKEN_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../scratch/live_token.json"))
ACCOUNT_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../scratch/ps5_account.json"))

def refresh_token_if_needed():
    acc = {}
    if os.path.exists(ACCOUNT_PATH):
        with open(ACCOUNT_PATH, "r", encoding="utf-8") as f:
            acc = json.load(f)
    elif os.path.exists(TOKEN_PATH):
        with open(TOKEN_PATH, "r", encoding="utf-8") as f:
            acc = json.load(f)

    refresh_token = acc.get("refresh_token")
    if not refresh_token:
        print("[!] No refresh_token found in account files")
        return acc.get("access_token")

    # If expires_at is in future by > 5 minutes, use access_token
    expires_at = acc.get("expires_at", 0)
    now = int(time.time())
    if expires_at - now > 300 and acc.get("access_token"):
        print(f"[*] Current access_token valid for another {expires_at - now}s")
        return acc.get("access_token")

    print("[*] Refreshing access token via Amazon MAP...")
    req_body = json.dumps({
        "app_name": "Amazon Luna",
        "app_version": "1.0.37063.0",
        "source_token_type": "refresh_token",
        "source_token": refresh_token,
        "requested_token_type": "access_token"
    }).encode("utf-8")

    req = urllib.request.Request(
        "https://api.amazon.com/auth/token",
        data=req_body,
        headers={"Content-Type": "application/json", "Accept-Language": "en_US"}
    )
    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode())
            new_token = data.get("access_token")
            expires_in = data.get("expires_in", 3600)
            acc["access_token"] = new_token
            acc["expires_at"] = int(time.time()) + int(expires_in)
            if "secureSessionToken" in data:
                acc["secureSessionToken"] = data["secureSessionToken"]
            with open(ACCOUNT_PATH, "w", encoding="utf-8") as f:
                json.dump(acc, f, indent=2)
            with open(TOKEN_PATH, "w", encoding="utf-8") as f:
                json.dump(acc, f, indent=2)
            print(f"[+] Token refreshed successfully! expires_in: {expires_in}s")
            return new_token
    except Exception as e:
        print(f"[-] Token refresh error: {e}")
        return acc.get("access_token")

def query_tempo(endpoint_path, payload_dict, access_token, region="eu"):
    base_url = "https://proxy-prod.eu-west-1.tempo.digital.a2z.com" if region == "eu" else "https://proxy-prod.tempo.digital.a2z.com"
    url = base_url + endpoint_path

    headers = {
        "x-amz-platform": "windows",
        "x-amz-device-type": "A1PY8QQU9P0WJV",
        "x-amz-access-token": access_token,
        "x-amz-marketplace-id": "A1RKKUPIHCS9HS",
        "x-amz-locale": "es_ES",
        "Accept-Language": "es-ES,es;q=0.9,en;q=0.8",
        "Content-Type": "application/json"
    }

    body = json.dumps(payload_dict).encode("utf-8")
    req = urllib.request.Request(url, data=body, headers=headers)

    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            data = resp.read()
            return resp.status, json.loads(data.decode("utf-8"))
    except urllib.error.HTTPError as he:
        body_err = ""
        try:
            body_err = he.read().decode("utf-8")
        except:
            pass
        return he.code, {"error": str(he), "body": body_err}
    except Exception as e:
        return 0, {"error": str(e)}

def save_dump(filename, data):
    filepath = os.path.join(DUMP_DIR, filename)
    with open(filepath, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    print(f"  -> Saved {filename} ({len(json.dumps(data))} bytes)")

def main():
    token = refresh_token_if_needed()
    if not token:
        print("[!] No access token available. Aborting.")
        return

    print(f"[*] Access token: {token[:20]}...")

    # Targets to probe
    targets = [
        # 1. multistate_navigation
        ("01_navigation.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "multistate_navigation"}
        }),
        # 2. landing_home (with serviceToken)
        ("02_landing_home_serviceToken.json", "/getPage", {
            "featureScheme": "",
            "serviceToken": "eyJlbmNyeXB0UGFnZUlkIjpmYWxzZSwicGFnZUlkIjoiZGVmYXVsdCIsInBhZ2VUeXBlIjoibGFuZGluZ19ob21lIn0="
        }),
        # 2b. landing_home (with pageContext)
        ("02b_landing_home_pageContext.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "landing_home"}
        }),
        # 3. multistate_game_night
        ("03_game_night.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "gamenight", "pageType": "smart_tv_gamehub"}
        }),
        # 3b. multistate_game_night alt
        ("03b_game_night_alt.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "multistate_game_night"}
        }),
        # 4. landing_library / my_stuff
        ("04_landing_library.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "landing_library"}
        }),
        # 5. Remote Play / couch
        ("05_remote_play.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "multistate_remote_play"}
        }),
        # 5b. couch alt
        ("05b_remote_play_couch.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "couch", "pageType": "multistate_remote_play"}
        }),
        # 6. Search
        ("06_search_v2.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "landing_search_v2"},
            "searchContext": {"query": "Sonic", "sort": "TITLE_A_TO_Z"}
        }),
        # 7. Game Details:
        # Fortnite
        ("07_gamedetail_fortnite.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "B0892K4Z7X", "pageType": "game_detail", "productStage": "Release"}
        }),
        # Bluey
        ("07_gamedetail_bluey.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "B0CM5Y2B1S", "pageType": "game_detail", "productStage": "Release"}
        }),
        # Hogwarts Legacy
        ("07_gamedetail_hogwarts.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "B0CQ8Q4X4W", "pageType": "game_detail", "productStage": "Release"}
        }),
        # Star Wars Racer
        ("07_gamedetail_starwars_racer.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "B08QVP2W7X", "pageType": "game_detail", "productStage": "Release"}
        }),
        # 8. Profiles & Settings
        ("08_profiles.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "multistate_profiles"}
        }),
        ("08b_settings.json", "/getPage", {
            "featureScheme": "",
            "pageContext": {"encryptPageId": False, "pageId": "default", "pageType": "landing_settings"}
        })
    ]

    for fname, path, payload in targets:
        print(f"[*] Querying {fname} via {path}...")
        status, data = query_tempo(path, payload, token)
        print(f"    Status: {status}")
        save_dump(fname, {"status": status, "payload_sent": payload, "response": data})
        time.sleep(0.3)

if __name__ == "__main__":
    main()
