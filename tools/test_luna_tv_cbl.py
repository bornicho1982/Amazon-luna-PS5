import urllib.request, json, uuid

dev_serial = uuid.uuid4().hex # 32-char hex string like Luna web uses

print(f"Testing with device_serial: {dev_serial}")

url = 'https://api.amazon.com/auth/create/codepair'
data = json.dumps({
    "code_data": {
        "domain": "Device",
        "device_name": "%FIRST_NAME%'s%DUPE_STRATEGY_1ST% LG TV",
        "app_name": "Amazon Luna",
        "app_version": "1.0.37063.0",
        "device_model": "LG TV",
        "device_serial": dev_serial,
        "device_type": "A2U9WJDSJD7QG9",
        "os_version": "999"
    }
}).encode()

req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json', 'Accept-Language': 'en_US'})
with urllib.request.urlopen(req) as resp:
    res = json.loads(resp.read().decode())
    print("Codepair response:", json.dumps(res, indent=2))
    pub = res['public_code']
    priv = res['private_code']

reg_url = 'https://api.amazon.com/auth/register'
reg_data = json.dumps({
    "auth_data": {
        "code_pair": {
            "public_code": pub,
            "private_code": priv
        }
    },
    "registration_data": {
        "domain": "Device",
        "device_name": "%FIRST_NAME%'s%DUPE_STRATEGY_1ST% LG TV",
        "app_name": "Amazon Luna",
        "app_version": "1.0.37063.0",
        "device_model": "LG TV",
        "device_serial": dev_serial,
        "device_type": "A2U9WJDSJD7QG9",
        "os_version": "999"
    },
    "requested_token_type": ["bearer"]
}).encode()

reg_req = urllib.request.Request(reg_url, data=reg_data, headers={'Content-Type': 'application/json', 'Accept-Language': 'en_US'})
try:
    with urllib.request.urlopen(reg_req) as resp:
        print("Register status:", resp.status)
        print("Register body:", resp.read().decode())
except urllib.error.HTTPError as e:
    print(f"Register status: HTTP {e.code}")
    print(f"Register body: {e.read().decode()}")
