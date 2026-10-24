#include "display/font_renderer.h"
#include "display/font_inter_bold.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "util/stb_truetype.h"

#include <cmath>

namespace luna::display {

FontRenderer::FontRenderer() {
    fontInfo_ = new stbtt_fontinfo();
}

FontRenderer::~FontRenderer() {
    if (fontInfo_) {
        delete static_cast<stbtt_fontinfo*>(fontInfo_);
        fontInfo_ = nullptr;
    }
}

bool FontRenderer::init() {
    if (isReady_) return true;

    auto* info = static_cast<stbtt_fontinfo*>(fontInfo_);
    int offset = stbtt_GetFontOffsetForIndex(kInterBoldFontData, 0);
    if (!stbtt_InitFont(info, kInterBoldFontData, offset)) {
        return false;
    }

    isReady_ = true;
    return true;
}

const CachedGlyph& FontRenderer::getGlyph(uint32_t codepoint, int fontSizePx) {
    static const CachedGlyph kEmpty;
    if (!isReady_) return kEmpty;

    auto key = std::make_pair(codepoint, fontSizePx);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }

    auto* info = static_cast<stbtt_fontinfo*>(fontInfo_);
    float scale = stbtt_ScaleForPixelHeight(info, static_cast<float>(fontSizePx));

    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(info, static_cast<int>(codepoint), &advance, &lsb);

    CachedGlyph glyph;
    glyph.advance = static_cast<int>(std::round(advance * scale));

    if (codepoint == ' ') {
        return cache_.emplace(key, std::move(glyph)).first->second;
    }

    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bmp = stbtt_GetCodepointBitmap(info, 0, scale, static_cast<int>(codepoint),
                                                   &w, &h, &xoff, &yoff);
    if (bmp) {
        glyph.width = w;
        glyph.height = h;
        glyph.xoff = xoff;
        glyph.yoff = yoff;
        glyph.bitmap.assign(bmp, bmp + (w * h));
        stbtt_FreeBitmap(bmp, nullptr);
    }

    return cache_.emplace(key, std::move(glyph)).first->second;
}

int FontRenderer::measureText(const std::string& text, int fontSizePx) {
    if (!isReady_ || text.empty()) return 0;
    int totalWidth = 0;
    size_t i = 0;
    while (i < text.size()) {
        uint8_t c0 = static_cast<uint8_t>(text[i]);
        uint32_t codepoint = 0;
        size_t seqLen = 1;

        if (c0 < 0x80) {
            codepoint = c0;
            seqLen = 1;
        } else if ((c0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
            codepoint = ((c0 & 0x1F) << 6) | (static_cast<uint8_t>(text[i + 1]) & 0x3F);
            seqLen = 2;
        } else if ((c0 & 0xF0) == 0xE0 && i + 2 < text.size()) {
            codepoint = ((c0 & 0x0F) << 12) | ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i + 2]) & 0x3F);
            seqLen = 3;
        } else if ((c0 & 0xF8) == 0xF0 && i + 3 < text.size()) {
            codepoint = ((c0 & 0x07) << 18) | ((static_cast<uint8_t>(text[i + 1]) & 0x3F) << 12) | ((static_cast<uint8_t>(text[i + 2]) & 0x3F) << 6) | (static_cast<uint8_t>(text[i + 3]) & 0x3F);
            seqLen = 4;
        } else {
            codepoint = c0;
            seqLen = 1;
        }
        i += seqLen;

        if (codepoint == 0x2122) { // ™
            totalWidth += fontSizePx;
            continue;
        }

        const auto& g = getGlyph(codepoint, fontSizePx);
        totalWidth += (g.advance > 0) ? g.advance : (fontSizePx / 2);
    }
    return totalWidth;
}

} // namespace luna::display
