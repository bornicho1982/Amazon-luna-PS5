with open('tools/luna_probe/chrome_profile/Default/IndexedDB/https_luna.amazon.es_0.indexeddb.leveldb/000003.log', 'rb') as f:
    data = f.read().decode('latin1', errors='ignore')

import re
matches = re.findall(r'amzn1\.luna\.product\.[a-f0-9\-]+', data)
unique_ids = list(dict.fromkeys(matches))

for pid in unique_ids[:15]:
    pos = data.find(pid)
    chunk = data[max(0, pos-200):min(len(data), pos+300)]
    # Look for title or titleText or name
    m = re.findall(r'\"([A-Za-z0-9\s:’\'-]{3,40})\"', chunk)
    print(f"ID: {pid}")
    print("  Context tokens:", m[:8])
