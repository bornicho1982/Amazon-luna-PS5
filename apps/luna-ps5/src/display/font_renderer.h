#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <utility>

namespace luna::display {

struct CachedGlyph {
    int width = 0;
    int height = 0;
    int xoff = 0;
    int yoff = 0;
    int advance = 0;
    std::vector<uint8_t> bitmap;
};

class FontRenderer {
public:
    FontRenderer();
    ~FontRenderer();

    bool init();

    int measureText(const std::string& text, int fontSizePx);
    const CachedGlyph& getGlyph(uint32_t codepoint, int fontSizePx);

private:
    void* fontInfo_ = nullptr;
    bool isReady_ = false;
    std::map<std::pair<uint32_t, int>, CachedGlyph> cache_;
};

} // namespace luna::display
