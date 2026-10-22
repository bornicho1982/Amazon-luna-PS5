import glob, re, os

for root, dirs, files in os.walk('.'):
    # skip node_modules, .git, reference/mbedtls
    if any(p in root for p in ['node_modules', '.git', 'deps', 'obj']): continue
    for f in files:
        if f.endswith('.js') or f.endswith('.py') or f.endswith('.cpp') or f.endswith('.h') or f.endswith('.json'):
            path = os.path.join(root, f)
            try:
                with open(path, 'r', encoding='utf-8', errors='ignore') as fp:
                    text = fp.read()
                if 'loginProfile' in text:
                    print(f'{path}: contains loginProfile')
            except:
                pass
