with open('tools/luna_probe/bundles/g6Kv31KlYEJ9v6u.js_name_vendor_amzn-42278a2c', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

import re
matches = [m.start() for m in re.finditer(r'PlayerLogin', text)]
for idx in matches:
    print(text[max(0, idx-50):min(len(text), idx+300)])
    print('='*50)
