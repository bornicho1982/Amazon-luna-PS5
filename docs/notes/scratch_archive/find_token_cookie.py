import os, glob

bundle_dir = r'tools/luna_probe/bundles'
for fpath in glob.glob(os.path.join(bundle_dir, '*')):
    if os.path.isfile(fpath):
        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
            c = f.read()
            for term in ['"/token"', '"/cookie"', "'/token'", "'/cookie'"]:
                pos = c.find(term)
                if pos != -1:
                    print(f'{os.path.basename(fpath)} has {term}:')
                    print(c[max(0, pos-100):min(len(c), pos+200)])
