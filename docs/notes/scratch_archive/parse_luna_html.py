import re

with open('docs/notes/scratch_archive/luna_official_page.html', 'r', encoding='utf-8') as f:
    html = f.read()

# Print links (css, fonts)
for l in re.findall(r'<link[^>]+>', html, re.I):
    print('LINK:', l)

# Print scripts
for s in re.findall(r'<script[^>]*src=["\']([^"\']+)["\'][^>]*>', html, re.I):
    print('SCRIPT SRC:', s)

# Print inline scripts with config
for m in re.finditer(r'<script[^>]*>(.*?)</script>', html, re.DOTALL):
    s_text = m.group(1)
    if 'config' in s_text or 'tempo' in s_text:
        print('=== INLINE SCRIPT (len %d) ===' % len(s_text))
        print(s_text)
