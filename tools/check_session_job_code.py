import glob, re

for path in glob.glob('tools/luna_probe/bundles/*.js*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    if 'createGameSessionJob' in text:
        pos = 0
        while True:
            m = text.find('createGameSessionJob', pos)
            if m == -1: break
            chunk = text[max(0, m-200):min(len(text), m+400)]
            print(f"Match in {path}:")
            print(chunk)
            print('='*50)
            pos = m + 20
