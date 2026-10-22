import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    for m in re.finditer(r'HardCodedPageType\.ProvisionGame', text):
        idx = m.start()
        print(f'{path} at {idx}:')
        print(text[max(0, idx-100):min(len(text), idx+200)])
        print('='*50)
