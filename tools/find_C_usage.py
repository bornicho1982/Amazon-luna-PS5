with open('tools/luna_probe/bundles/WFT5TZ2yQrxmCeL.js_name_initial_app-f41317b2', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

import re
matches = [m.start() for m in re.finditer(r'\bC\.A\b', text)]
for idx in matches:
    print(text[max(0, idx-100):min(len(text), idx+200)])
    print('='*50)
