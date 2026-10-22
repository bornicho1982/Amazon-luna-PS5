import glob, re

for path in glob.glob('tools/luna_probe/bundles/*.js*'):
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        text = f.read()
    urls = set(re.findall(r'https?://[a-zA-Z0-9\.\-_/]+', text))
    amazon_urls = [u for u in urls if 'amazon' in u or 'tempo' in u]
    if amazon_urls:
        print(f"=== {path} ===")
        for u in sorted(amazon_urls)[:10]:
            print("  ", u)
