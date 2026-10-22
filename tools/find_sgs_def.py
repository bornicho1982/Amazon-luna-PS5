with open('tools/luna_probe/bundles/lTN6T6JCBar8tug.js_name_initial_app-c41feb9a', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = 31955
print(text[max(0, idx-1500):min(len(text), idx+500)])
