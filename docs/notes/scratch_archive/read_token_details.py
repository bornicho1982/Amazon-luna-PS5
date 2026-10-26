with open('tools/luna_probe/bundles/WFT5TZ2yQrxmCeL.js_name_initial_app-f41317b2', 'r', encoding='utf-8') as f:
    text = f.read()

pos = text.find('.url("/token")')
print(text[pos-200:pos+800])
