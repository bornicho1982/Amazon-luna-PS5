import os, glob, re

bundle_dir = r'tools/luna_probe/bundles'
for fpath in glob.glob(os.path.join(bundle_dir, '*')):
    if os.path.isfile(fpath):
        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
            c = f.read()
            for term in ['displayIndex', 'showIndex', 'rankedCarousel', 'isRanked', 'rankNumber', 'rankIndex']:
                pos = c.find(term)
                if pos != -1:
                    print(f'{os.path.basename(fpath)} @ {pos} has {term}:')
                    print(c[max(0, pos-50):min(len(c), pos+150)])
