with open('tools/luna_probe/bundles/0JvYmn1EZxOU-A8.js', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('handleSignIn()')
if idx != -1:
    print(text[max(0, idx-100):min(len(text), idx+1500)])
