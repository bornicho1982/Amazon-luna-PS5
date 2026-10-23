import urllib.request, json, time, os, sys

def make_request(url, payload_dict, headers_dict):
    data = json.dumps(payload_dict).encode('utf-8') if payload_dict is not None else b''
    headers = {
        'Content-Type': 'application/json',
        'Content-Length': str(len(data))
    }
    headers.update(headers_dict)
    req = urllib.request.Request(url, data=data, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            body = resp.read().decode('utf-8')
            return resp.status, body
    except urllib.error.HTTPError as e:
        body = e.read().decode('utf-8') if e.fp else ''
        return e.code, body
    except Exception as e:
        return 0, str(e)

def run_tests_with_token(access_token, customer_id, device_serial):
    print("="*60)
    print("INICIANDO BATERIA DE PRUEBAS CON TOKEN REAL")
    print(f"Token: {access_token[:20]}... (len {len(access_token)})")
    print(f"Customer ID: {customer_id}")
    print(f"Device Serial: {device_serial}")
    print("="*60)

    hosts = [
        "https://proxy-prod.eu-west-1.tempo.digital.a2z.com",
        "https://proxy-prod.tempo.digital.a2z.com"
    ]

    marketplaces = [
        ("A1RKKUPIHCS9HS", "ES", "es-ES", "Europe/Madrid"),
        ("ATVPDKIKX0DER", "US", "en-US", "America/New_York")
    ]

    platforms = [
        ("smart_tv_web", "A2U9WJDSJD7QG9", "LG TV"),
        ("web", "A1PY8QQU9P0WJV", "Chrome")
    ]

    # Paso 1: getAppConfig
    for host in hosts:
        for m_id, cor, loc, tz in marketplaces:
            for plat, dev_type, dev_model in platforms:
                h = {
                    'x-amz-platform': plat,
                    'x-amz-device-type': dev_type,
                    'x-amz-marketplace-id': m_id,
                    'x-amz-locale': loc,
                    'x-amz-access-token': access_token
                }
                status, body = make_request(f"{host}/getAppConfig", {}, h)
                print(f"[getAppConfig] {host} ({m_id}, {plat}) -> HTTP {status}: {body[:100]}")

    # Paso 2: getIotConnectionInfo
    tdi = ""
    for host in hosts:
        h = {
            'x-amz-access-token': access_token,
            'x-amz-platform': 'smart_tv_web',
            'x-amz-device-type': 'A2U9WJDSJD7QG9',
            'x-amz-marketplace-id': 'A1RKKUPIHCS9HS',
            'x-amz-locale': 'es-ES'
        }
        status, body = make_request(f"{host}/getIotConnectionInfo", {}, h)
        print(f"[getIotConnectionInfo] {host} -> HTTP {status}: {body[:150]}")
        if status == 200:
            try:
                tdi = json.loads(body).get('tdi', '')
                print(f"  --> TDI extraido: {tdi[:30]}...")
            except:
                pass

    # Paso 3: playerLogin
    profile_token = ""
    profile_attempts = [
        {"profileId": customer_id},
        {"profileId": "default"},
        {}
    ]
    for host in hosts:
        for attempt in profile_attempts:
            h = {
                'x-amz-access-token': access_token,
                'x-amz-platform': 'smart_tv_web',
                'x-amz-device-type': 'A2U9WJDSJD7QG9',
                'x-amz-marketplace-id': 'A1RKKUPIHCS9HS',
                'x-amz-locale': 'es-ES'
            }
            if tdi: h['x-amz-tdi'] = tdi
            status, body = make_request(f"{host}/player/playerLogin", attempt, h)
            print(f"[playerLogin] {host} {attempt} -> HTTP {status}: {body[:150]}")
            if status == 200:
                try:
                    profile_token = json.loads(body).get('token', '')
                    print(f"  --> PROFILE TOKEN extraido: {profile_token[:30]}...")
                    break
                except:
                    pass
        if profile_token: break

    # Paso 4: getPage
    page_contexts = [
        {"pageId": "default", "pageType": "multistate_navigation"},
        {"pageId": "default", "pageUri": "home"},
        {"pageId": "default", "pageType": "home"},
        {"pageId": "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16", "pageType": "provision_game"}
    ]

    for host in hosts:
        for m_id, cor, loc, tz in marketplaces:
            for plat, dev_type, dev_model in platforms:
                for ctx in page_contexts:
                    h = {
                        'x-amz-access-token': access_token,
                        'x-amz-platform': plat,
                        'x-amz-device-type': dev_type,
                        'x-amz-client-version': '1.0.37063.0',
                        'x-amz-react-version': '1.0.37063.0',
                        'x-amz-marketplace-id': m_id,
                        'x-amz-country-of-residence': cor,
                        'x-amz-timezone': tz,
                        'x-amz-locale': loc,
                        'Accept-Language': loc,
                        'x-amz-device-serial-number': device_serial
                    }
                    if tdi: h['x-amz-tdi'] = tdi
                    if profile_token: h['x-amz-profile-token'] = profile_token

                    payload = {
                        "featureScheme": "default",
                        "pageContext": ctx
                    }
                    status, body = make_request(f"{host}/getPage", payload, h)
                    tag = "*** EXITO 200 OK! ***" if status == 200 else f"HTTP {status}"
                    print(f"[getPage] {tag} {host} ({m_id}, {plat}, {ctx.get('pageType') or ctx.get('pageUri')}) -> {body[:200]}")
                    if status == 200:
                        print("="*60)
                        print("¡¡¡COMBINACION GANADORA DETECTADA!!!")
                        print(f"Host: {host}")
                        print(f"Marketplace: {m_id}")
                        print(f"Platform: {plat}")
                        print(f"Headers utilizados: {json.dumps(h, indent=2)}")
                        print(f"Payload utilizado: {json.dumps(payload, indent=2)}")
                        print(f"Respuesta (primeros 500 caracteres):\n{body[:500]}")
                        print("="*60)
                        return True
    return False

if __name__ == '__main__':
    # Check if account.json exists locally
    account_file = "account.json"
    if os.path.exists(account_file):
        with open(account_file, "r") as f:
            acc = json.load(f)
            run_tests_with_token(acc.get("access_token", ""), acc.get("customer_id", "AmazonUser"), acc.get("device_serial", "c16fe0a195a4464d8c2ed7c8ebb83f6b"))
            sys.exit(0)

    print("No hay account.json local.")
