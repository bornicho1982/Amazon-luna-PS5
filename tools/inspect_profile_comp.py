with open('tools/luna_probe/bundles/9O1dTM_gNCO4yXQ.js_name_initial_app-a3083e9c', 'r', encoding='utf-8', errors='ignore') as f:
    text = f.read()

idx = 61846
print(text[max(0, idx-600):min(len(text), idx+1000)])
