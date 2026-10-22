with open('tools/luna_probe/bundles/lTN6T6JCBar8tug.js_name_initial_app-c41feb9a', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = 33616
idx2 = text.find('.displayName', idx)
print(text[idx:idx2+50])
