with open('tools/luna_probe/bundles/lTN6T6JCBar8tug.js_name_initial_app-c41feb9a', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

import re
matches = [m.start() for m in re.finditer(r'provision\s*\([^)]*\)\s*\{', text)]
for idx in matches:
    print(text[max(0, idx-100):min(len(text), idx+600)])
    print('='*50)
