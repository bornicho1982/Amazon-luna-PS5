import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    idx = 0
    while True:
        idx = text.find('.setToken(', idx)
        if idx == -1: break
        print(f'{path} at {idx}:')
        print(text[max(0, idx-100):min(len(text), idx+300)])
        print('='*50)
        idx += 10
