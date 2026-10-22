import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    matches = re.findall(r'getAccessToken\s*\([^)]*\)\s*\{', text)
    if matches:
        print(f'{path}: {matches}')
