import re

with open('tools/luna_probe/bundles/NHnqA85uo6j9fRd.js', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

urls = set(re.findall(r'https?://[a-zA-Z0-9\.\-_/]+', text))
luna_urls = [u for u in urls if 'luna' in u or 'amazon' in u]
for u in sorted(luna_urls):
    print(u)
