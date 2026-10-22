import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    m = re.search(r'([a-zA-Z0-9_]+)\.HardCodedPageType\s*=\s*\{([^}]+)\}', text)
    if m:
        print(f'{path}:')
        print(m.group(0)[:1000])
        print('='*50)
    else:
        m2 = re.search(r'HardCodedPageType\s*=\s*\{([^}]+)\}', text)
        if m2:
            print(f'{path}:')
            print(m2.group(0)[:1000])
            print('='*50)
