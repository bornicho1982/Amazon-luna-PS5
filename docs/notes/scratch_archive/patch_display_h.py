path = r'apps/luna-ps5/src/display/display_manager.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

target = '    int64_t flipMarker_ = 0;\n};'
replacement = '''    int64_t flipMarker_ = 0;

    std::vector<uint8_t> heroBannerRgba_;
    int heroBannerWidth_ = 0;
    int heroBannerHeight_ = 0;
    void loadStaticHeroBanner();
};'''

if target in text:
    text = text.replace(target, replacement)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    print("Patched display_manager.h successfully.")
else:
    print("Target not found in display_manager.h")
