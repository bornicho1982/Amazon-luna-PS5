import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    # Look for pageType containing profile or select
    m = re.findall(r'pageType["\']?\s*:\s*["\']([^"\']+)["\']', text)
    if m:
        for pt in set(m):
            if any(k in pt.lower() for k in ['profile', 'select', 'player', 'multi', 'household', 'account']):
                print(f'{path}: pageType = {pt}')
