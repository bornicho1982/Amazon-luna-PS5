with open('tools/luna_probe/bundles/25FnWx2xCo40Q-y.js_name_initial_app-ad52a362', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = text.find('getWidgetFromToken')
if idx != -1:
    print(text[max(0, idx-100):min(len(text), idx+500)])
