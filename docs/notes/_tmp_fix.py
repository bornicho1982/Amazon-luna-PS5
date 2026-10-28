p = 'tools/luna_probe/patch_v2_display.py'
s = open(p, encoding='utf-8').read()
s = s.replace("decode('utf-8', 'replace')", "decode('utf-8')")
s = s.replace('        centerText(0, 0, "", 0);\n', '')
open(p, 'w', encoding='utf-8').write(s)
