import glob, re

targets = set()
for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    for m in re.finditer(r'target\s*===?\s*["\']([A-Z0-9_]+)["\']', text):
        targets.add(m.group(1))

for t in sorted(targets):
    print(t)
