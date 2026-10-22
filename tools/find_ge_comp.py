with open('tools/luna_probe/bundles/WFT5TZ2yQrxmCeL.js_name_initial_app-f41317b2', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = 16331
print(text[max(0, idx-500):min(len(text), idx+500)])
