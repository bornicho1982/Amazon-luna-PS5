import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    m = re.findall(r'pageType\s*:\s*([a-zA-Z0-9_\.\$]+)', text)
    if m:
        print(f'{path}: {set(m)}')
