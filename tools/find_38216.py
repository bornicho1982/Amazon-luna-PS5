import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    if '38216' in text:
        pos = text.find('38216')
        print(f'{path} at {pos}:')
        print(text[max(0, pos-100):min(len(text), pos+300)])
        print('='*50)
