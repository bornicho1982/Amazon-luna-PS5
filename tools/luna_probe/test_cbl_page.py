import urllib.request
import json

url = "https://proxy-prod.tempo.digital.a2z.com/getPage"
headers = {
    "Content-Type": "application/json",
    "x-amz-device-type": "A1PY8QQU9P0WJV",
    "x-amz-locale": "es-ES",
    "x-amz-marketplace-id": "A1RKKUPIHCS9HS",
    "x-amzn-RequestId": "12345678-1234-4234-8234-123456789012"
}

body = {
    "pageContext": {
        "pageType": "code_based_linking",
        "pageId": "default"
    }
}

req = urllib.request.Request(url, data=json.dumps(body).encode('utf-8'), headers=headers)
try:
    with urllib.request.urlopen(req) as resp:
        data = resp.read().decode('utf-8')
        print(f"Status: {resp.status}")
        print("Response:", data[:1000])
except urllib.error.HTTPError as e:
    print(f"HTTPError: {e.code}")
    print("Error body:", e.read().decode('utf-8')[:1000])
except Exception as e:
    print("Error:", e)
