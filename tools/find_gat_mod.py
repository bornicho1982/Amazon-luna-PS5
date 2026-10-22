with open('tools/luna_probe/bundles/25FnWx2xCo40Q-y.js_name_initial_app-ad52a362', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('getAccessToken(!1,!0)')
# search backwards for require or import
snippet = text[max(0, idx-2000):idx]
import re
m = re.findall(r'([a-zA-Z0-9_]+)\.getAccessToken', snippet)
print(m)
# find module numbers
m2 = re.findall(r'[a-zA-Z0-9_]+\s*=\s*r\(([0-9]+)\)', snippet)
print('required modules:', m2)
