with open('tools/luna_probe/bundles/g6Kv31KlYEJ9v6u.js_name_vendor_amzn-42278a2c', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('CreatePlayerFromProfileProxyRequest')
if idx != -1:
    print(text[max(0, idx-200):min(len(text), idx+1000)])
