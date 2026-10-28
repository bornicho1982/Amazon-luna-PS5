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

# Test 1: getPartiesForPlayer
req1 = urllib.request.Request("https://proxy-prod.eu-west-1.tempo.digital.a2z.com/party/getPartiesForPlayer", data=b"{}", headers=headers)
try:
    with urllib.request.urlopen(req1) as resp:
        d1 = json.loads(resp.read().decode("utf-8"))
        print("[+] getPartiesForPlayer HTTP 200:", d1)
        with open("tools/luna_probe/dumps/05d_get_parties.json", "w", encoding="utf-8") as out:
            json.dump({"response": d1}, out, indent=2)
except Exception as e:
    print("[-] getPartiesForPlayer error:", e)

# Test 2: createParty
req2 = urllib.request.Request("https://proxy-prod.eu-west-1.tempo.digital.a2z.com/party/createParty", data=b"{}", headers=headers)
try:
    with urllib.request.urlopen(req2) as resp:
        d2 = json.loads(resp.read().decode("utf-8"))
        print("[+] createParty HTTP 200:", d2)
        with open("tools/luna_probe/dumps/05e_create_party.json", "w", encoding="utf-8") as out:
            json.dump({"response": d2}, out, indent=2)
except Exception as e:
    print("[-] createParty error:", e)
