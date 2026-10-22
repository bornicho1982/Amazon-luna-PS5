with open('tools/luna_probe/bundles/lTN6T6JCBar8tug.js_name_initial_app-c41feb9a', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

import re
matches = [m.start() for m in re.finditer(r'playToken', text)]
print(f'Total playToken matches: {len(matches)}')
for idx in matches[:10]:
    print(text[max(0, idx-100):min(len(text), idx+200)])
    print('='*50)
