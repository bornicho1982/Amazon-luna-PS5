import urllib.request
import json

url = "https://api.amazon.com/auth/O2/create/codepair"
# Standard LWA CBL request
data = {
    "response_type": "device_code",
    "client_id": "amzn1.application-oa2-client.e79d501b44c6439589d97faee7c3faef", # sample / test or empty
    "scope": "cbl:guest"
}

req = urllib.request.Request(
    url,
    data=json.dumps(data).encode('utf-8'),
    headers={"Content-Type": "application/json"}
)

try:
    with urllib.request.urlopen(req) as resp:
        print("Success:", resp.read().decode())
except urllib.error.HTTPError as e:
    print(f"HTTPError {e.code}: {e.read().decode()}")
except Exception as e:
    print("Error:", e)
