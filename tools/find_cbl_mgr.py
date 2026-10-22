import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    if 'CBLAuthManager' in text:
        pos = text.find('CBLAuthManager')
        print(f'{path} at {pos}:')
        print(text[max(0, pos-100):min(len(text), pos+1000)])
        print('='*50)
