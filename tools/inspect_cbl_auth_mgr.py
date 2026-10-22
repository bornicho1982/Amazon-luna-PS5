with open('tools/luna_probe/bundles/NHnqA85uo6j9fRd.js', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('89316:')
if idx != -1:
    print(text[idx:idx+3500])
