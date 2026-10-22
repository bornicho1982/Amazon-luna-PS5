import glob, re

for path in glob.glob('tools/luna_probe/bundles/*.js*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    for m in re.finditer(r'https?://[a-zA-Z0-9\.\-_/]+luna[a-zA-Z0-9\.\-_/]*', text):
        print(f"{path}: {m.group(0)}")
