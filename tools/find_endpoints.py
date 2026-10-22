import re, glob

endpoints = set()
for path in glob.glob('tools/luna_probe/bundles/*.js'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    for m in re.finditer(r'https?://[a-zA-Z0-9\.\-_]+\.a2z\.com/[a-zA-Z0-9_\-/]+|https?://luna\.amazon\.[a-z]+/[a-zA-Z0-9_\-/]+', text):
        endpoints.add(m.group(0))

for ep in sorted(endpoints):
    print(ep)
