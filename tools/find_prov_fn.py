with open('tools/luna_probe/bundles/lTN6T6JCBar8tug.js_name_initial_app-c41feb9a', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('this.provision()')
if idx != -1:
    print(text[max(0, idx-200):min(len(text), idx+600)])
else:
    idx2 = text.find('provision()')
    print(text[max(0, idx2-200):min(len(text), idx2+600)])
