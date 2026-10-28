import urllib.request
import json

with open("scratch/live_token.json") as f:
    token = json.load(f)["access_token"]

# 1. Do playerLogin
login_body = json.dumps({"profileId": "amzn1.actor.person.oid.A24FP4VJ7KI5CD"}).encode("utf-8")
headers = {
    "x-amz-platform": "smart_tv_web",
    "x-amz-device-type": "A2U9WJDSJD7QG9",
    "x-amz-marketplace-id": "A1RKKUPIHCS9HS",
    "x-amz-locale": "es_ES",
    "x-amz-access-token": token,
    "Content-Type": "application/json"
}

req_login = urllib.request.Request("https://proxy-prod.eu-west-1.tempo.digital.a2z.com/player/playerLogin", data=login_body, headers=headers)
with urllib.request.urlopen(req_login) as resp:
    player_token = json.loads(resp.read().decode("utf-8"))["token"]
    print("[+] Got player_token!")

headers["x-amz-player-token"] = player_token

# 2. Test createParty
req_party = urllib.request.Request("https://proxy-prod.eu-west-1.tempo.digital.a2z.com/party/createParty", data=b"{}", headers=headers)
try:
    with urllib.request.urlopen(req_party) as resp:
        d = json.loads(resp.read().decode("utf-8"))
        print("[+] createParty SUCCESS HTTP 200:", d)
        with open("tools/luna_probe/dumps/05e_create_party_success.json", "w", encoding="utf-8") as out:
            json.dump({"response": d}, out, indent=2)
except urllib.error.HTTPError as e:
    print(f"[-] createParty HTTP {e.code}: {e.read().decode()}")

# 3. Test getPartiesForPlayer
req_get_parties = urllib.request.Request("https://proxy-prod.eu-west-1.tempo.digital.a2z.com/party/getPartiesForPlayer", data=b"{}", headers=headers)
try:
    with urllib.request.urlopen(req_get_parties) as resp:
        d = json.loads(resp.read().decode("utf-8"))
        print("[+] getPartiesForPlayer SUCCESS HTTP 200:", d)
        with open("tools/luna_probe/dumps/05d_get_parties_success.json", "w", encoding="utf-8") as out:
            json.dump({"response": d}, out, indent=2)
except urllib.error.HTTPError as e:
    print(f"[-] getPartiesForPlayer HTTP {e.code}: {e.read().decode()}")
