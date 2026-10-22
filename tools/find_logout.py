with open('tools/luna_probe/bundles/ioFfxG_QSGdADeb.js_name_initial_app-8261290d', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('Received profile logout')
if idx != -1:
    print(text[max(0, idx-300):min(len(text), idx+1000)])
