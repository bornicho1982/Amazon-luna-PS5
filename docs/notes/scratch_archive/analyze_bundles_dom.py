import os, glob, re

bundle_dir = r'tools/luna_probe/bundles'
classes = set()
for fpath in glob.glob(os.path.join(bundle_dir, '*')):
    if os.path.isfile(fpath):
        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
            c = f.read()
            for m in re.findall(r'className:\s*["\']([^"\']+)["\']', c):
                for cls in m.split():
                    if len(cls) > 3:
                        classes.add(cls)

print('Sample classes:', sorted(list(classes))[:60])
print('Total unique classes:', len(classes))
