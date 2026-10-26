with open('tools/luna_probe/bundles/E9gQkAQ2DbbeLGm.js_name_initial_app-b559ca9f', 'r', encoding='utf-8') as f:
    text = f.read()

pos = text.find('featuredRotatorImageWrapper')
print(text[pos+1000:pos+2500])
