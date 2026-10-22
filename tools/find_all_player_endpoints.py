import glob, re

endpoints = set()
for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    for m in re.finditer(r'["\']player/([a-zA-Z0-9_-]+)["\']', text):
        endpoints.add(m.group(0))

for ep in sorted(endpoints):
    print(ep)
