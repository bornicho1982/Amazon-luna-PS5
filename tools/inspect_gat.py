with open('tools/luna_probe/bundles/WFT5TZ2yQrxmCeL.js_name_initial_app-f41317b2', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('getAccessToken(e=!1,t=!1){')
if idx != -1:
    print(text[idx:idx+1500])
