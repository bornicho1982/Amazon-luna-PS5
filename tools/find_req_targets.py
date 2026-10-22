with open('tools/luna_probe/bundles/25FnWx2xCo40Q-y.js_name_initial_app-ad52a362', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

import re
matches = re.findall(r'requestWithToken\([\'"]([^\'"]+)[\'"]', text)
print('requestWithToken targets:', sorted(set(matches)))
matches2 = re.findall(r'doRequest\([\'"]([^\'"]+)[\'"]', text)
print('doRequest targets:', sorted(set(matches2)))
