import os, glob, re

urls = set()
files = glob.glob('tools/luna_probe/bundles/*') + glob.glob('apps/luna-ps5/src/**/*.*', recursive=True) + glob.glob('scratch/*.*')
for fpath in files:
    if os.path.isfile(fpath):
        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
            c = f.read()
            for m in re.findall(r'https://m\.media-amazon\.com/images/[^\s"\'<>)]+', c):
                urls.add(m)

print(f'Found {len(urls)} Amazon image URLs:')
for u in sorted(list(urls)):
    print(u)
