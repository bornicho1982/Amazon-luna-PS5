with open('tools/luna_probe/bundles/8I4cnFRo9NwF2_B.js_name_runtime', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

import re
m = re.findall(r'"([a-zA-Z0-9_\-]+)":"([a-zA-Z0-9_\-]+)"', text)
print(f'Total chunk mappings: {len(m)}')
for k, v in m[:50]:
    print(f'{k} -> {v}')
