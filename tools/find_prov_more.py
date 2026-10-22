with open('tools/luna_probe/bundles/lTN6T6JCBar8tug.js_name_initial_app-c41feb9a', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('StartGameSession.provision.duplicate')
print(text[idx:idx+1500])
