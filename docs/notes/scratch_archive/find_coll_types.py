import os, glob, re

bundle_dir = r'tools/luna_probe/bundles'
coll_types = set()
for fpath in glob.glob(os.path.join(bundle_dir, '*')):
    if os.path.isfile(fpath):
        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
            c = f.read()
            for m in re.findall(r'([A-Z0-9_]+_COLLECTION|[A-Z0-9_]+_CAROUSEL|[A-Z0-9_]+_ROTATOR)', c):
                coll_types.add(m)

print('Found collection types:', coll_types)
