with open('tools/luna_probe/bundles/0JvYmn1EZxOU-A8.js', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

import re
matches = re.findall(r'https://api\.amazon\.com/[a-zA-Z0-9_\-/]+', text)
print('api.amazon.com calls in 0JvYmn1EZxOU-A8.js:', set(matches))
matches_paths = re.findall(r'["\'](/auth/[a-zA-Z0-9_\-/]+)["\']', text)
print('auth paths in 0JvYmn1EZxOU-A8.js:', set(matches_paths))
