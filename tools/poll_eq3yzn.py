import urllib.request, json

pub = 'EQ3YZN'
priv = '2b989500-c0dd-4726-ac95-278ac050eb2b'

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
        "device_name": "User's PlayStation 5",
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
        print('Status:', resp.status)
        print('Body:', resp.read().decode())
except urllib.error.HTTPError as e:
    print('HTTPError:', e.code, e.read().decode())
