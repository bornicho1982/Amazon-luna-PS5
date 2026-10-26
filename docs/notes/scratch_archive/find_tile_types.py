import os, glob, re

bundle_dir = r'tools/luna_probe/bundles'
tile_types = set()
for fpath in glob.glob(os.path.join(bundle_dir, '*')):
    if os.path.isfile(fpath):
        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
            c = f.read()
            for m in re.findall(r'([A-Z0-9_]+_TILE)', c):
                tile_types.add(m)

print('Found tile types:', tile_types)
