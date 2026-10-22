import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    idx = text.find('CreatePlayerFromProfile')
    if idx != -1:
        print(f'{path} at {idx}:')
        print(text[max(0, idx-50):min(len(text), idx+500)])
        print('='*50)
