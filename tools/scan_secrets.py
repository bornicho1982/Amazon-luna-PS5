import os, re

PATTERNS = [
    re.compile(r'Atnr\|[A-Za-z0-9_-]{10,}'),
    re.compile(r'Atza\|[A-Za-z0-9_-]{10,}'),
    re.compile(r'amzn1\.account\.[A-Za-z0-9]{15,}'),
    re.compile(r'amzn1\.oa2-cs\.v1\.[A-Za-z0-9_-]{15,}'),
    re.compile(r'"access_token":\s*"[^"]{15,}"'),
    re.compile(r'"refresh_token":\s*"[^"]{15,}"'),
    re.compile(r'password\s*=\s*["\'][^"\']+["\']', re.I),
]

IGNORED_DIRS = {'.git', 'chrome_profile', 'reference', 'ps5-payload-sdk', 'node_modules', '.venv', 'build', 'bundles'}

found = []
for root, dirs, files in os.walk('.'):
    dirs[:] = [d for d in dirs if d not in IGNORED_DIRS]
    for file in files:
        if file.endswith(('.png', '.ttf', '.so', '.bin', '.prx', '.elf', '.o', '.obj')):
            continue
        path = os.path.join(root, file)
        try:
            with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                for pat in PATTERNS:
                    m = pat.search(content)
                    if m:
                        found.append((path, m.group(0)[:40]))
        except Exception as e:
            pass

if found:
    print("WARNING: Found potential secrets:")
    for p, val in found:
        print(f"  {p}: {val}")
else:
    print("CLEAN: No personal credentials or tokens detected in repo files!")
