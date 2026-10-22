import re, json

with open('tools/luna_probe/bundles/8I4cnFRo9NwF2_B.js_name_runtime', 'r', encoding='utf-8', errors='ignore') as fp:
    c = fp.read()

chunks = [4359, 4652, 6654, 6015, 94, 6990, 4997, 493, 1404, 9999, 6857, 5485, 157, 682, 691, 8254, 2780, 3254, 1343, 7706, 7689, 3907, 1031, 8872, 6916, 4817, 7940, 5159]
for chunk in chunks:
    # in webpack runtime: e.u = e => "" + ... or map: {123: "abc"}
    m = re.search(r'\b' + str(chunk) + r':"([^"]+)"', c)
    if m:
        print(f"chunk {chunk} -> {m.group(1)}")
