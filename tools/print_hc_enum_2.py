with open('tools/luna_probe/bundles/g6Kv31KlYEJ9v6u.js_name_vendor_amzn-42278a2c', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('t.HardCodedPageType=void 0,function(e){')
if idx != -1:
    print(text[idx+2500:idx+6000])
