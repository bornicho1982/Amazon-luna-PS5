with open('tools/luna_probe/bundles/vXpFAgbXCwovXmX.js_name_initial_app-be2e8c93', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = 64237
print(text[max(0, idx-500):min(len(text), idx+1500)])
