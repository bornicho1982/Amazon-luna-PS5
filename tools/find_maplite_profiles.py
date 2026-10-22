with open('tools/luna_probe/bundles/0JvYmn1EZxOU-A8.js', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

import re
matches = re.findall(r'["\'][a-zA-Z0-9_\-/]*profile[a-zA-Z0-9_\-/]*["\']', text, re.IGNORECASE)
print('matches in 0JvYmn1EZxOU-A8.js:', set(matches))
matches2 = re.findall(r'https?://[a-zA-Z0-9_\-\./]+', text)
print('urls in 0JvYmn1EZxOU-A8.js:', set(matches2))
