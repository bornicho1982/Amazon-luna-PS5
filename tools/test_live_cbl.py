import urllib.request, json, time

url = 'https://api.amazon.com/auth/create/codepair'
data = json.dumps({
    "code_data": {
        "domain": "Device",
        "device_name": "%FIRST_NAME%'s PlayStation 5",
        "app_name": "Luna",
        "app_version": "1.0.37063.0",
        "device_model": "PlayStation 5",
        "device_serial": "ps5-dsn-001",
        "device_type": "A1PY8QQU9P0WJV",
        "os_version": "PlayStation 5"
    }
}).encode()

req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json', 'Accept-Language': 'en_US'})
with urllib.request.urlopen(req) as resp:
    res = json.loads(resp.read().decode())
    pub = res['public_code']
    priv = res['private_code']

print(f"=== NUEVO CODIGO GENERADO: {pub} ===")
print("Payload de codepair:", res)

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
        "device_name": "%FIRST_NAME%'s PlayStation 5",
        "app_name": "Luna",
        "app_version": "1.0.37063.0",
        "device_model": "PlayStation 5",
        "device_serial": "ps5-dsn-001",
        "device_type": "A1PY8QQU9P0WJV",
        "os_version": "PlayStation 5"
    },
    "requested_token_type": ["bearer"]
}).encode()

reg_req = urllib.request.Request(reg_url, data=reg_data, headers={'Content-Type': 'application/json', 'Accept-Language': 'en_US'})
try:
    with urllib.request.urlopen(reg_req) as resp:
        print("Register status:", resp.status)
        print("Register body:", resp.read().decode())
except urllib.error.HTTPError as e:
    print(f"Initial test poll: HTTP {e.code}: {e.read().decode()}")
