import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    matches = re.findall(r'["\'](?:https?://[^"\']+|/[a-zA-Z0-9_\-/]+)["\']', text)
    for m in matches:
        if any(k in m.lower() for k in ['/profile', 'profiles', 'household', 'family']):
            print(f'{path}: {m}')
