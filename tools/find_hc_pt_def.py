import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    idx = 0
    while True:
        idx = text.find('HardCodedPageType', idx)
        if idx == -1: break
        # check if it's enum definition
        snippet = text[max(0, idx-50):min(len(text), idx+500)]
        if '={' in snippet or '||(' in snippet or 'function(' in snippet:
            print(f'{path} at {idx}:')
            print(snippet)
            print('='*50)
            break
        idx += 17
