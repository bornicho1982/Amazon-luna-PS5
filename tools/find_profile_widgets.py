import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    idx = 0
    while True:
        idx = text.find('.profileId', idx)
        if idx == -1: break
        snippet = text[max(0, idx-80):min(len(text), idx+120)]
        if any(w in snippet for w in ['widget', 'presentation', 'item', 'payload', 'profile']):
            print(f'{path}: {snippet}')
            print('-'*40)
        idx += 10
