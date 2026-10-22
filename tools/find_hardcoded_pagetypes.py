import glob, re

for path in glob.glob('tools/luna_probe/bundles/*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    
    if 'HardCodedPageType' in text:
        pos = text.find('HardCodedPageType=')
        if pos == -1: pos = text.find('HardCodedPageType')
        print(f'{path}:')
        print(text[pos:pos+1500])
        print('='*50)
