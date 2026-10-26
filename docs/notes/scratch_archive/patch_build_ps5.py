path = r'apps/luna-ps5/build_ps5.ps1'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

target = '        "display/display_manager",\n'
replacement = '        "display/display_manager",\n        "display/hero_banner_asset",\n'

if target in text:
    text = text.replace(target, replacement, 1)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    print("Added hero_banner_asset to build_ps5.ps1 successfully.")
else:
    print("Target not found in build_ps5.ps1")
