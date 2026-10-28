"""Patch display_manager.h/.cpp: fast swizzled primitives, cached clear, alpha masks, 2-section GameDetail,
search screen without letter bar, profile screens, settings sections, starting-session scene snapshot."""
import re

H = 'apps/luna-ps5/src/display/display_manager.h'
C = 'apps/luna-ps5/src/display/display_manager.cpp'


def load(p):
    s = open(p, 'rb').read().decode('utf-8')
    return s.replace('\r\r\n', '\n').replace('\r\n', '\n')


def save(p, s):
    open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))


def rep(s, old, new, count=1):
    assert old in s, 'NOT FOUND: ' + old[:80]
    return s.replace(old, new, count) if count else s.replace(old, new)


def between(s, start, end, new, include_end=False):
    a = s.index(start)
    b = s.index(end, a)
    if include_end:
        b += len(end)
    return s[:a] + new + s[b:]


# ---------------------------------------------------------------- HEADER
h = load(H)
h = rep(h, '''    void drawSettingsScreen(int selectedItemIdx,
                            const std::string& statusText,
                            uint64_t frameCount);''', '''    void drawSettingsScreen(int selectedItemIdx,
                            const std::string& statusText,
                            uint64_t frameCount,
                            int accountsSubTab = 0);

    // kind: 0 = Gestionar perfiles, 1 = Planes Luna, 2 = Distintivos, 3 = Stream
    void drawProfileScreen(int kind,
                           int sel,
                           int subTab,
                           const PlayerProfileInfo& profile,
                           const std::vector<GameTile>& games,
                           uint64_t frameCount);

    void setSearchQuery(const std::string& q) { searchQuery_ = q; }
    // The next present() call is swallowed so a modal can be drawn over the current scene in the same frame.
    void deferNextPresent() { deferPresent_ = true; }''')
h = rep(h, '''    void loadStaticHeroBanner();
};''', '''    void loadStaticHeroBanner();

    void blendRectUniform(int x, int y, int w, int h, uint32_t colorArgb, uint8_t alpha);
    void applyAlphaMask(const std::vector<uint8_t>& mask, int maskW, int maskH,
                        const std::vector<int>& rowStart, const std::vector<int>& rowEnd,
                        int destX, int destY, uint32_t colorArgb);
    void ensureMasks();

    // Pre-computed render caches (built once, reused every frame)
    std::vector<uint8_t> bgCache_;
    uint32_t bgCacheColor_ = 0;
    std::vector<uint8_t> sceneCache_;
    std::string sceneKey_;
    std::vector<uint8_t> heroMask_;
    std::vector<int> heroRowStart_, heroRowEnd_;
    std::vector<uint8_t> detailMask_;
    std::vector<int> detailRowStart_, detailRowEnd_;
    bool masksReady_ = false;
    float detailScrollY_ = 0.0f;
    std::string searchQuery_;
    bool deferPresent_ = false;
};''')
save(H, h)

# ---------------------------------------------------------------- CPP
c = load(C)

# 1. fast tile tables
c = rep(c, '''// Convierte ARGB (0xAARRGGBB) a ABGR''', '''// Precomputed per-column / per-row XOR terms of the macro-tile swizzle (avoids recomputing 9 shifts per pixel)
struct FastTile {
    uint32_t xt[kWidth + 128];
    uint32_t yt[kHeight + 128];
    FastTile() {
        for (unsigned x = 0; x < kWidth + 128; ++x) {
            xt[x] = ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^ ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
        }
        for (unsigned y = 0; y < kHeight + 128; ++y) {
            yt[y] = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^ ((y << 8) & 0x4000U);
        }
    }
};

inline const FastTile& getFastTile() {
    static const FastTile t;
    return t;
}

inline size_t fastOffset(const FastTile& ft, unsigned x, unsigned y) {
    return (static_cast<size_t>((y >> 7) * ((kWidth + 127U) >> 7) + (x >> 7)) << 16) + (ft.yt[y] ^ ft.xt[x]);
}

// Convierte ARGB (0xAARRGGBB) a ABGR''')

c = c.replace('size_t offset = tiledByteOffset(static_cast<unsigned>(x), static_cast<unsigned>(y));',
              'size_t offset = fastOffset(getFastTile(), static_cast<unsigned>(x), static_cast<unsigned>(y));')

# 2. clear() with cached pre-swizzled background
c = between(c, 'void DisplayManager::clear(uint32_t colorArgb) {', 'void DisplayManager::fillRect(', r'''void DisplayManager::clear(uint32_t colorArgb) {
    if (!isReady_ || !mappedMemory_) return;

    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    uint32_t abgr = argbToAbgr(colorArgb);

#if defined(LUNA_TARGET_PS5)
    // Fast path: one memcpy of a pre-swizzled background snapshot
    if (!bgCache_.empty() && bgCacheColor_ == abgr) {
        std::memcpy(base, bgCache_.data(), kUsedBytes);
        return;
    }
    const TileOrder& order = getTileOrder();
    constexpr unsigned kTotalTiles = kTilesX * kTilesY;
    for (unsigned t = 0; t < kTotalTiles; ++t) {
        unsigned tx = t % kTilesX;
        unsigned ty = t / kTilesX;
        auto* out = reinterpret_cast<uint32_t*>(base + static_cast<size_t>(t) * 0x10000);
        for (unsigned i = 0; i < 16384; ++i) {
            unsigned x = tx * 128 + order.dx[i];
            unsigned y = ty * 128 + order.dy[i];
            out[i] = (x < kWidth && y < kHeight) ? abgr : 0xFF000000U;
        }
    }
    bgCache_.assign(base, base + kUsedBytes);
    bgCacheColor_ = abgr;
#else
    auto* buf = reinterpret_cast<uint32_t*>(base);
    for (int i = 0; i < width_ * height_; ++i) {
        buf[i] = abgr;
    }
#endif
}

''')

# 3. fillRect fast
c = between(c, 'void DisplayManager::fillRect(', 'void DisplayManager::drawImage(', r'''void DisplayManager::fillRect(int rx, int ry, int rw, int rh, uint32_t colorArgb) {
    if (!isReady_ || !mappedMemory_) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    int x0 = rx < 0 ? 0 : rx;
    int y0 = ry < 0 ? 0 : ry;
    int x1 = (rx + rw) > width_ ? width_ : (rx + rw);
    int y1 = (ry + rh) > height_ ? height_ : (ry + rh);
    if (x0 >= x1 || y0 >= y1) return;

#if defined(LUNA_TARGET_PS5)
    const FastTile& ft = getFastTile();
    const uint32_t abgr = argbToAbgr(colorArgb);
    constexpr unsigned kBlocksPerRow = (kWidth + 127U) >> 7;
    for (int y = y0; y < y1; ++y) {
        const size_t rowBlk = static_cast<size_t>(static_cast<unsigned>(y) >> 7) * kBlocksPerRow;
        const uint32_t yt = ft.yt[y];
        for (int x = x0; x < x1; ++x) {
            const size_t off = ((rowBlk + (static_cast<unsigned>(x) >> 7)) << 16) + (yt ^ ft.xt[x]);
            *reinterpret_cast<uint32_t*>(base + off) = abgr;
        }
    }
#else
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            setPixelSwizzled(base, x, y, colorArgb);
        }
    }
#endif
}

''')

# 4. drawPreScaledImage fast + new helpers
c = between(c, 'void DisplayManager::drawPreScaledImage(', 'void DisplayManager::drawPlayTriangleIcon(', r'''void DisplayManager::drawPreScaledImage(int rx, int ry, const uint8_t* rgba, int dstW, int dstH, int srcW, int srcH) {
    if (!isReady_ || !mappedMemory_ || !rgba || dstW <= 0 || dstH <= 0) return;
    if (srcW <= 0) srcW = dstW;
    if (srcH <= 0) srcH = dstH;
    if (srcW > 4096 || srcH > 4096) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);

    int x0 = std::clamp(rx, 0, width_);
    int y0 = std::clamp(ry, 0, height_);
    int x1 = std::clamp(rx + dstW, 0, width_);
    int y1 = std::clamp(ry + dstH, 0, height_);
    if (x0 >= x1 || y0 >= y1) return;

#if defined(LUNA_TARGET_PS5)
    const FastTile& ft = getFastTile();
    constexpr unsigned kBlocksPerRow = (kWidth + 127U) >> 7;
    const bool oneToOne = (srcW == dstW && srcH == dstH);
    static int xmap[kWidth + 128];
    if (!oneToOne) {
        for (int x = x0; x < x1; ++x) {
            int sx = static_cast<int>((static_cast<int64_t>(x - rx) * srcW) / dstW);
            xmap[x] = sx >= srcW ? srcW - 1 : sx;
        }
    }
    for (int y = y0; y < y1; ++y) {
        int srcY = oneToOne ? (y - ry) : static_cast<int>((static_cast<int64_t>(y - ry) * srcH) / dstH);
        if (srcY >= srcH) srcY = srcH - 1;
        const uint8_t* rowSrc = rgba + static_cast<size_t>(srcY) * static_cast<size_t>(srcW) * 4;
        const size_t rowBlk = static_cast<size_t>(static_cast<unsigned>(y) >> 7) * kBlocksPerRow;
        const uint32_t yt = ft.yt[y];
        for (int x = x0; x < x1; ++x) {
            const int sx = oneToOne ? (x - rx) : xmap[x];
            const uint8_t* p = rowSrc + static_cast<size_t>(sx) * 4;
            const uint8_t a = p[3];
            if (a == 0) continue;
            const size_t off = ((rowBlk + (static_cast<unsigned>(x) >> 7)) << 16) + (yt ^ ft.xt[x]);
            uint32_t* dst = reinterpret_cast<uint32_t*>(base + off);
            if (a == 255) {
                uint32_t px;
                std::memcpy(&px, p, 4); // RGBA bytes == little-endian ABGR word expected by the framebuffer
                *dst = px;
            } else {
                const uint32_t inv = 255U - a;
                const uint32_t d = *dst;
                const uint32_t r = (p[0] * a + (d & 0xFF) * inv) / 255U;
                const uint32_t g = (p[1] * a + ((d >> 8) & 0xFF) * inv) / 255U;
                const uint32_t b = (p[2] * a + ((d >> 16) & 0xFF) * inv) / 255U;
                *dst = 0xFF000000U | (b << 16) | (g << 8) | r;
            }
        }
    }
#else
    size_t totalSrcBytes = static_cast<size_t>(srcW) * static_cast<size_t>(srcH) * 4;
    float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);
    for (int y = y0; y < y1; ++y) {
        int srcY = std::clamp(static_cast<int>((y - ry) * scaleY), 0, srcH - 1);
        size_t rowOffset = static_cast<size_t>(srcY) * static_cast<size_t>(srcW) * 4;
        for (int x = x0; x < x1; ++x) {
            int srcX = std::clamp(static_cast<int>((x - rx) * scaleX), 0, srcW - 1);
            size_t pixelOffset = rowOffset + (static_cast<size_t>(srcX) * 4);
            if (pixelOffset + 3 >= totalSrcBytes) continue;
            const uint8_t* p = rgba + pixelOffset;
            uint8_t a = p[3];
            uint32_t argb = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
            if (a == 255) setPixelSwizzled(base, x, y, argb);
            else if (a > 0) blendPixelSwizzled(base, x, y, argb, a);
        }
    }
#endif
}

void DisplayManager::blendRectUniform(int rx, int ry, int rw, int rh, uint32_t colorArgb, uint8_t alpha) {
    if (!isReady_ || !mappedMemory_ || alpha == 0) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    int x0 = std::max(0, rx);
    int y0 = std::max(0, ry);
    int x1 = std::min(width_, rx + rw);
    int y1 = std::min(height_, ry + rh);
    if (x0 >= x1 || y0 >= y1) return;
#if defined(LUNA_TARGET_PS5)
    const FastTile& ft = getFastTile();
    constexpr unsigned kBlocksPerRow = (kWidth + 127U) >> 7;
    const uint32_t pr = ((colorArgb >> 16) & 0xFF) * alpha;
    const uint32_t pg = ((colorArgb >> 8) & 0xFF) * alpha;
    const uint32_t pb = (colorArgb & 0xFF) * alpha;
    const uint32_t inv = 255U - alpha;
    for (int y = y0; y < y1; ++y) {
        const size_t rowBlk = static_cast<size_t>(static_cast<unsigned>(y) >> 7) * kBlocksPerRow;
        const uint32_t yt = ft.yt[y];
        for (int x = x0; x < x1; ++x) {
            const size_t off = ((rowBlk + (static_cast<unsigned>(x) >> 7)) << 16) + (yt ^ ft.xt[x]);
            uint32_t* p = reinterpret_cast<uint32_t*>(base + off);
            const uint32_t d = *p;
            const uint32_t r = (pr + (d & 0xFF) * inv) / 255U;
            const uint32_t g = (pg + ((d >> 8) & 0xFF) * inv) / 255U;
            const uint32_t b = (pb + ((d >> 16) & 0xFF) * inv) / 255U;
            *p = 0xFF000000U | (b << 16) | (g << 8) | r;
        }
    }
#else
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) blendPixelSwizzled(base, x, y, colorArgb, alpha);
    }
#endif
}

void DisplayManager::applyAlphaMask(const std::vector<uint8_t>& mask, int maskW, int maskH,
                                    const std::vector<int>& rowStart, const std::vector<int>& rowEnd,
                                    int destX, int destY, uint32_t colorArgb) {
    if (!isReady_ || !mappedMemory_ || mask.empty()) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    const uint32_t sr = (colorArgb >> 16) & 0xFF;
    const uint32_t sg = (colorArgb >> 8) & 0xFF;
    const uint32_t sb = colorArgb & 0xFF;
#if defined(LUNA_TARGET_PS5)
    const FastTile& ft = getFastTile();
    constexpr unsigned kBlocksPerRow = (kWidth + 127U) >> 7;
#endif
    for (int my = 0; my < maskH; ++my) {
        const int y = destY + my;
        if (y < 0 || y >= height_) continue;
        int xs = std::max(0, rowStart[my]);
        int xe = std::min(maskW, rowEnd[my]);
        const uint8_t* mrow = mask.data() + static_cast<size_t>(my) * maskW;
        for (int mx = xs; mx < xe; ++mx) {
            const uint32_t a = mrow[mx];
            if (a == 0) continue;
            const int x = destX + mx;
            if (x < 0 || x >= width_) continue;
#if defined(LUNA_TARGET_PS5)
            const size_t off = ((static_cast<size_t>(static_cast<unsigned>(y) >> 7) * kBlocksPerRow + (static_cast<unsigned>(x) >> 7)) << 16) + (ft.yt[y] ^ ft.xt[x]);
            uint32_t* p = reinterpret_cast<uint32_t*>(base + off);
            const uint32_t d = *p;
            const uint32_t inv = 255U - a;
            const uint32_t r = (sr * a + (d & 0xFF) * inv) / 255U;
            const uint32_t g = (sg * a + ((d >> 8) & 0xFF) * inv) / 255U;
            const uint32_t b = (sb * a + ((d >> 16) & 0xFF) * inv) / 255U;
            *p = 0xFF000000U | (b << 16) | (g << 8) | r;
#else
            blendPixelSwizzled(base, x, y, colorArgb, static_cast<uint8_t>(a));
#endif
        }
    }
}

void DisplayManager::ensureMasks() {
    if (masksReady_) return;
    masksReady_ = true;

    // Hero banner mask (1920x540): dark fade on the left side and bottom edge
    heroMask_.assign(static_cast<size_t>(1920) * 540, 0);
    heroRowStart_.assign(540, 1920);
    heroRowEnd_.assign(540, 0);
    for (int y = 0; y < 540; ++y) {
        float yAlpha = (y > 350) ? std::min(1.0f, static_cast<float>(y - 350) / 190.0f) : 0.0f;
        uint8_t* row = heroMask_.data() + static_cast<size_t>(y) * 1920;
        for (int x = 0; x < 950; ++x) {
            float xAlpha = (x < 700) ? 1.0f - static_cast<float>(x) / 700.0f : 0.0f;
            float t = std::min(1.0f, xAlpha * 0.95f + yAlpha * 0.95f);
            if (t > 0.01f) row[x] = static_cast<uint8_t>(t * 255.0f);
        }
        if (y > 400) {
            float b = std::min(1.0f, static_cast<float>(y - 400) / 140.0f);
            for (int x = 950; x < 1920; ++x) row[x] = static_cast<uint8_t>(b * 255.0f);
        }
        for (int x = 0; x < 1920; ++x) {
            if (row[x]) { heroRowStart_[y] = x; break; }
        }
        for (int x = 1919; x >= 0; --x) {
            if (row[x]) { heroRowEnd_[y] = x + 1; break; }
        }
    }

    // Game detail mask (1920x1080): dark fade left + bottom
    detailMask_.assign(static_cast<size_t>(1920) * 1080, 0);
    detailRowStart_.assign(1080, 1920);
    detailRowEnd_.assign(1080, 0);
    for (int y = 0; y < 1080; ++y) {
        float yAlpha = (y > 480) ? std::min(1.0f, static_cast<float>(y - 480) / 380.0f) : 0.0f;
        uint8_t* row = detailMask_.data() + static_cast<size_t>(y) * 1920;
        for (int x = 0; x < 1050; ++x) {
            float xAlpha = (x < 850) ? 1.0f - static_cast<float>(x) / 850.0f : 0.0f;
            float t = std::min(1.0f, xAlpha * 0.95f + yAlpha * 0.95f);
            if (t > 0.01f) row[x] = static_cast<uint8_t>(t * 255.0f);
        }
        if (y > 520) {
            float b = std::min(0.92f, static_cast<float>(y - 520) / 340.0f);
            for (int x = 1050; x < 1920; ++x) row[x] = static_cast<uint8_t>(b * 255.0f);
        }
        for (int x = 0; x < 1920; ++x) {
            if (row[x]) { detailRowStart_[y] = x; break; }
        }
        for (int x = 1919; x >= 0; --x) {
            if (row[x]) { detailRowEnd_[y] = x + 1; break; }
        }
    }
}

''')

# 5. check glyph helper
c = rep(c, '''} // namespace

void DisplayManager::drawText(''', '''
// Vector check mark (the UI font has no reliable U+2713 glyph)
void drawCheckGlyph(DisplayManager& d, int x, int y, int s, uint32_t c) {
    for (int i = 0; i <= s / 3; ++i) d.fillRect(x + i, y + s / 2 + i, 3, 3, c);
    for (int i = 0; i <= (s * 2) / 3; ++i) d.fillRect(x + s / 3 + i, y + s / 2 + s / 3 - i, 3, 3, c);
}
} // namespace

void DisplayManager::drawText(''')

# 6. present() deferral
c = rep(c, '''void DisplayManager::present() {
    if (!isReady_) return;
''', '''void DisplayManager::present() {
    if (!isReady_) return;
    if (deferPresent_) {
        deferPresent_ = false;
        return;
    }
''')

# 7. modals: translucent dim instead of solid fills
c = c.replace('fillRect(0, 0, 1920, 1080, 0xD0000000);', 'blendRectUniform(0, 0, 1920, 1080, 0xFF000000, 0xD0);')
c = rep(c, 'fillRect(0, 0, 1920, 1080, 0xDD07050A);', 'clear(0xFF0E0B16);')

# 8. hero gradient -> cached mask
c = between(c, '// Degradado oscuro suave sobre el Hero Banner', '// Lado izquierdo del Hero (x = 68):',
            'ensureMasks();\n                applyAlphaMask(heroMask_, 1920, 540, heroRowStart_, heroRowEnd_, 0, heroY, 0xFF0E0B16);\n\n                ')

# 9. profile popup labels (official dropdown)
c = between(c, 'static const char* kOfficialProfileOpts[10] = {', '};', '''static const char* kOfficialProfileOpts[10] = {
            "Gestionar perfiles",
            "Planes Luna",
            "Distintivos",
            "Stream",
            "Cuentas vinculadas",
            "Idioma",
            "Configuración",
            "Novedades de Luna",
            "Ayuda",
            "Acerca de Luna"
        ''')

# 10. search: new early-return screen at top of home, remove old block
search_new = r'''    // VISTA DE BUSQUEDA OFICIAL: barra "Buscar" + teclado compacto + cuadricula 5 columnas 16:9
    if (searchOpen) {
        clear(0xFF0E0B16);
        const bool barFoc = (searchZone == 0);
        drawRoundedRect(72, 36, 1776, 58, 12, 0xFF1C152E);
        drawRoundedBorder(72, 36, 1776, 58, 12, barFoc ? 3 : 1, barFoc ? 0xFFFFFFFF : 0xFF2D2248);
        if (searchQuery_.empty()) {
            drawText(100, 50, 2, "Buscar", 0xFF94A3B8);
        } else {
            drawText(100, 50, 2, searchQuery_ + (((frameCount / 30) % 2 == 0) ? "|" : ""), 0xFFFFFFFF);
        }
        {
            int mgx = 1806, mgy = 64, mgr = 9;
            drawRoundedBorder(mgx - mgr, mgy - mgr, mgr * 2, mgr * 2, mgr, 2, 0xFFCBD5E1);
            for (int d = 0; d < 7; ++d) fillRect(mgx + mgr - 3 + d, mgy + mgr - 3 + d, 2, 2, 0xFFCBD5E1);
        }

        static const char* kKeys[28] = {
            "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
            "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "ESPACIO", "BORRAR"
        };
        for (int k = 0; k < 28; ++k) {
            int kx = 72 + (k % 14) * 126;
            int ky = 112 + (k / 14) * 50;
            bool kf = (searchZone == 0 && searchLetterIdx == k);
            drawRoundedRect(kx, ky, 118, 42, 8, kf ? 0xFF9146FF : 0xFF1C152E);
            int tw = fontRenderer_.measureText(kKeys[k], 16);
            drawText(kx + (118 - tw) / 2, ky + 10, 1, kKeys[k], kf ? 0xFFFFFFFF : 0xFFCBD5E1);
        }

        const int numF = static_cast<int>(filteredGames.size());
        drawText(72, 222, 1, std::to_string(numF) + " resultados", 0xFF94A3B8);

        const int gCardW = 310, gCardH = 174, gGapX = 56, gPitchY = 236, gBaseY = 252;
        int firstRow = (searchZone == 1) ? std::max(0, searchGridIdx / 5 - 2) : 0;
        for (int g = firstRow * 5; g < numF && g < firstRow * 5 + 15; ++g) {
            int col = g % 5;
            int row = (g / 5) - firstRow;
            int gx = 72 + col * (gCardW + gGapX);
            int gy = gBaseY + row * gPitchY;
            const auto& tile = filteredGames[g];
            std::string imgUrl = tile.backdropUrl.empty() ? tile.coverUrl : tile.backdropUrl;
            auto cImg = luna::util::ImageLoader::instance().getImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
            if (cImg && cImg->ready) {
                drawPreScaledImage(gx, gy, cImg->rgba.data(), gCardW, gCardH, cImg->width, cImg->height);
            } else {
                if (!imgUrl.empty()) luna::util::ImageLoader::instance().requestImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
                drawRoundedRect(gx, gy, gCardW, gCardH, 12, 0xFF241B3B);
            }
            if (searchZone == 1 && searchGridIdx == g) {
                drawRoundedBorder(gx - 3, gy - 3, gCardW + 6, gCardH + 6, 14, 3, 0xFFFFFFFF);
            }
            drawText(gx, gy + gCardH + 8, 2, utf8SafeTruncate(tile.title, 26), 0xFFFFFFFF);
        }
        if (!statusText.empty()) {
            fillRect(0, 1046, 1920, 34, 0xEE0B0912);
            drawText(44, 1054, 1, statusText, 0xFF94A3B8);
        }
        present();
        return;
    }

'''
c = rep(c, '''    // 1. Fondo principal oficial de Amazon Luna (#0E0B16 Cosmic Deep)
    clear(0xFF0E0B16);
''', '''    // 1. Fondo principal oficial de Amazon Luna (#0E0B16 Cosmic Deep)
    clear(0xFF0E0B16);

''' + search_new)
a = c.index('    // 7. VISTA DE BUSQUEDA EN CUADRICULA DE 5 COLUMNAS EN 16:9')
b = c.index('    present();\n}\n', a)
c = c[:a] + c[b:]

# 11. GameDetail: 2-section vertical scroll
detail_new = r'''void DisplayManager::drawGameDetailScreen(const GameTile& game,
                                          const PlayerProfileInfo& profile,
                                          const std::string& statusText,
                                          bool isConnecting,
                                          int detailZone,
                                          int detailBtnIdx,
                                          int similarSelectedIdx,
                                          uint64_t frameCount,
                                          bool isFavorite) {
    if (!isReady_ || !mappedMemory_) return;
    (void)profile;
    (void)frameCount;
    ensureMasks();

    // Zones: 0 buttons, 1 trailer/screenshots (top view) | 3 FAQs, 2 similar games (bottom view)
    constexpr float kBottomScroll = 880.0f;
    const float target = (detailZone >= 2) ? kBottomScroll : 0.0f;
    detailScrollY_ += (target - detailScrollY_) * 0.40f;
    if (std::fabs(target - detailScrollY_) < 2.0f) detailScrollY_ = target;
    const int sy = static_cast<int>(detailScrollY_);
    const float progress = detailScrollY_ / kBottomScroll;

    clear(0xFF0E0B16);

    if (progress < 0.99f) {
        std::string bgUrl = game.backdropUrl.empty() ? game.coverUrl : game.backdropUrl;
        auto bgImg = luna::util::ImageLoader::instance().getImage(bgUrl, luna::util::ImagePreset::PanoramicDetail);
        if (!bgImg || !bgImg->ready) {
            bgImg = luna::util::ImageLoader::instance().getImage(bgUrl, luna::util::ImagePreset::PanoramicHero);
        }
        if (bgImg && bgImg->ready) {
            drawPreScaledImage(0, 0, bgImg->rgba.data(), 1920, 1080, bgImg->width, bgImg->height);
        } else {
            if (!bgUrl.empty()) luna::util::ImageLoader::instance().requestImage(bgUrl, luna::util::ImagePreset::PanoramicDetail);
            fillRect(0, 0, 1920, 1080, 0xFF140F24);
        }
        applyAlphaMask(detailMask_, 1920, 1080, detailRowStart_, detailRowEnd_, 0, 0, 0xFF0E0B16);
        if (progress > 0.01f) {
            blendRectUniform(0, 0, 1920, 1080, 0xFF0E0B16, static_cast<uint8_t>(std::min(1.0f, progress * 1.05f) * 240.0f));
        }
    }

    // Fixed top bar
    blendRectUniform(0, 0, 1920, 68, 0xFF0E0B16, 150);
    drawText(44, 22, 2, "<  Volver  (O)", 0xFFE2E8F0);
    {
        int magX = 1754, magY = 32, magR = 8;
        drawRoundedBorder(magX - magR, magY - magR, magR * 2, magR * 2, magR, 2, 0xFFCBD5E1);
        for (int d = 0; d < 6; ++d) fillRect(magX + magR - 3 + d, magY + magR - 3 + d, 2, 2, 0xFFCBD5E1);
        drawRoundedRect(1810, 16, 38, 38, 19, 0xFF14B8A6);
        drawText(1822, 24, 2, "B", 0xFFFFFFFF);
    }

    const int ty = -sy;

    // ===================== TOP VIEW =====================
    if (progress < 0.9f) {
        bool drawnLogo = false;
        if (!game.logoUrl.empty()) {
            auto logoImg = luna::util::ImageLoader::instance().getImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
            if (logoImg && logoImg->ready) {
                drawPreScaledImage(72, ty + 110, logoImg->rgba.data(), logoImg->width, logoImg->height, logoImg->width, logoImg->height);
                drawnLogo = true;
            } else {
                luna::util::ImageLoader::instance().requestImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
            }
        }
        if (!drawnLogo) {
            drawText(72, ty + 150, 6, utf8SafeTruncate(game.title, 26), 0xFFFFFFFF);
        }

        std::string meta = game.genres.empty() ? "Acción, Aventura" : game.genres;
        meta += "  \xE2\x80\xA2  " + (game.playerCount.empty() ? std::string("1 jugador") : game.playerCount);
        meta += "  \xE2\x80\xA2  Remote Play";
        drawText(72, ty + 296, 2, utf8SafeTruncate(meta, 90), 0xFFCBD5E1);

        const int btnY = ty + 350;
        const bool primFocused = (detailZone == 0 && detailBtnIdx == 0);
        const bool favFocused = (detailZone == 0 && detailBtnIdx == 1);
        const bool rpFocused = (detailZone == 0 && detailBtnIdx == 2);
        if (game.isEntitled) {
            drawPillButton(72, btnY, 260, 60, isConnecting ? "CONECTANDO..." : "Jugar ahora", primFocused, "", true);
        } else {
            std::string pri = game.buttonText.empty() ? "Incluido en Luna Premium" : game.buttonText;
            drawPillButton(72, btnY, 260, 60, pri, primFocused, "9,99 EUR/mes");
        }
        drawRoundedRect(344, btnY, 60, 60, 14, favFocused ? 0xFFFFFFFF : 0xFF2D2248);
        drawStarIcon(374, btnY + 30, 12, isFavorite ? 0xFFF59E0B : (favFocused ? 0xFF0E0B16 : 0xFFFFFFFF));
        drawRoundedRect(416, btnY, 210, 60, 14, rpFocused ? 0xFF9146FF : 0xFF2D2248);
        drawRoundedBorder(416, btnY, 210, 60, 14, rpFocused ? 2 : 1, rpFocused ? 0xFFFFFFFF : 0xFF4C1D95);
        drawText(440, btnY + 18, 2, "Remote Play", 0xFFFFFFFF);

        if (game.isEntitled) {
            drawCheckGlyph(*this, 72, ty + 436, 18, 0xFF4DA3FF);
            drawText(98, ty + 432, 2, "Incluido con Prime", 0xFF4DA3FF);
        }
        drawText(72, ty + 478, 2, "Compartir", 0xFF94A3B8);
        drawText(200, ty + 478, 2, "Añadir al escritorio", 0xFF94A3B8);
        drawText(430, ty + 478, 2, "Informar de un problema", 0xFF94A3B8);
        drawText(72, ty + 522, 1, "Al jugar aceptas las condiciones de uso de Amazon Luna y el aviso de privacidad de Amazon.", 0xFF64748B);
        drawText(72, ty + 546, 1, "Los juegos de terceros pueden recopilar datos conforme a sus propias politicas de privacidad.", 0xFF64748B);

        // Small PEGI seal over the clean artwork
        {
            uint32_t pegiBg = 0xFFF59E0B;
            std::string pegiNum = "12";
            if (game.pegiRating.find("18") != std::string::npos) { pegiBg = 0xFFDC2626; pegiNum = "18"; }
            else if (game.pegiRating.find("16") != std::string::npos) { pegiBg = 0xFFEA580C; pegiNum = "16"; }
            else if (game.pegiRating.find("7") != std::string::npos || game.pegiRating.find("3") != std::string::npos) { pegiBg = 0xFF10B981; pegiNum = "7"; }
            drawRoundedRect(1760, ty + 380, 60, 68, 8, pegiBg);
            drawText(1774, ty + 388, 1, "PEGI", 0xFF0E0B16);
            drawText(1772, ty + 408, 3, pegiNum, 0xFF0E0B16);
        }

        // Trailer & screenshots row (16:9 thumbnails)
        drawText(72, ty + 660, 3, "Tráiler y capturas de pantalla", 0xFFFFFFFF);
        const size_t numScreens = game.screenshotUrls.size();
        for (size_t t = 0; t < numScreens && t < 5; ++t) {
            int tx = 72 + static_cast<int>(t) * (290 + 24);
            int thY = ty + 704;
            const std::string& imgUrl = game.screenshotUrls[t];
            auto thImg = luna::util::ImageLoader::instance().getImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
            if (thImg && thImg->ready) {
                drawPreScaledImage(tx, thY, thImg->rgba.data(), 290, 164, thImg->width, thImg->height);
            } else {
                luna::util::ImageLoader::instance().requestImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
                drawRoundedRect(tx, thY, 290, 164, 12, 0xFF241B3B);
            }
            if (t == 0 && !game.videoUrl.empty()) {
                drawRoundedRect(tx + 121, thY + 58, 48, 48, 24, 0xCC0E0B16);
                drawPlayTriangleIcon(tx + 145, thY + 82, 10, 0xFFFFFFFF);
            }
            if (detailZone == 1 && similarSelectedIdx == static_cast<int>(t)) {
                drawRoundedBorder(tx - 3, thY - 3, 296, 170, 14, 3, 0xFFFFFFFF);
            }
        }
    }

    // ===================== BOTTOM VIEW =====================
    auto visible = [&](int vy, int h, int limit) { int y = vy - sy; return (y + h > -10) && (y < limit); };

    // Description (left column)
    if (visible(980, 40, 1060)) drawText(72, 980 + ty, 4, "Descripción", 0xFFFFFFFF);
    if (visible(1030, 260, 1000)) {
        std::string desc = game.synopsis.empty()
            ? std::string("Disfruta de este juego en Amazon Luna con streaming a 1080p y 60 FPS desde la nube, sin descargas ni instalaciones.")
            : game.synopsis;
        auto lines = utf8WrapLines(desc, 92, 9);
        int dy = 1034 + ty;
        for (const auto& l : lines) {
            drawText(72, dy, 2, l, 0xFFCBD5E1);
            dy += 28;
        }
    }

    // FAQs
    if (visible(1290, 230, 1000)) {
        drawText(72, 1290 + ty, 4, "Preguntas frecuentes", 0xFFFFFFFF);
        static const char* kFaqTitles[3] = {
            "¿Qué es Amazon Luna?",
            "¿Qué incluye la suscripción Prime?",
            "¿Qué es la suscripción Luna Premium?"
        };
        for (int q = 0; q < 3; ++q) {
            int qy = 1344 + q * 58 + ty;
            bool qFoc = (detailZone == 3 && similarSelectedIdx == q);
            drawRoundedRect(72, qy, 1050, 48, 10, qFoc ? 0xFF9146FF : 0xFF1C152E);
            drawRoundedBorder(72, qy, 1050, 48, 10, qFoc ? 3 : 1, qFoc ? 0xFFFFFFFF : 0xFF2D2248);
            drawText(96, qy + 13, 2, kFaqTitles[q], 0xFFFFFFFF);
            drawText(1086, qy + 11, 2, "+", qFoc ? 0xFFFFFFFF : 0xFF94A3B8);
        }
    }

    // Specifications card (right column)
    if (visible(980, 520, 1000)) {
        int cx = 1180, cy = 980 + ty, cw = 668, chh = 520;
        drawRoundedRect(cx, cy, cw, chh, 16, 0xEE161024);
        drawRoundedBorder(cx, cy, cw, chh, 16, 1, 0xFF2D2248);
        drawText(cx + 28, cy + 24, 3, "Especificaciones", 0xFFFFFFFF);
        fillRect(cx + 28, cy + 66, cw - 56, 1, 0xFF352654);
        struct SpecRow { const char* label; std::string value; };
        std::vector<SpecRow> specs = {
            {"Lanzamiento", game.releaseYear.empty() ? "2025" : game.releaseYear},
            {"Distribuidora", utf8SafeTruncate(game.publisher.empty() ? "Amazon Luna" : game.publisher, 28)},
            {"Desarrolladores", utf8SafeTruncate(game.developer.empty() ? "Amazon Games" : game.developer, 28)},
            {"Gameplay", game.playerCount.empty() ? "1 jugador" : game.playerCount},
            {"Vídeo", "Hasta 1080p a 60 FPS"},
            {"Entradas", utf8SafeTruncate(game.inputMethods.empty() ? "Mando, Teclado y ratón" : game.inputMethods, 28)},
            {"Idiomas", "Español, English, Français, Deutsch"},
            {"PEGI", game.pegiRating.empty() ? "PEGI 12" : game.pegiRating}
        };
        int sRowY = cy + 84;
        for (const auto& sp : specs) {
            drawText(cx + 28, sRowY, 2, sp.label, 0xFF94A3B8);
            drawText(cx + 250, sRowY, 2, sp.value, 0xFFFFFFFF);
            sRowY += 54;
        }
    }

    // Similar games carousel
    if (visible(1580, 280, 1060) && !game.similarGames.empty()) {
        drawText(72, 1580 + ty, 4, "También te puede interesar", 0xFFFFFFFF);
        int posterW = 150, posterH = 210, gap = 22;
        for (size_t s = 0; s < game.similarGames.size() && s < 10; ++s) {
            int sx = 72 + static_cast<int>(s) * (posterW + gap);
            if (sx + posterW > 1920) break;
            int py = 1634 + ty;
            const auto& simTile = game.similarGames[s];
            auto sImg = luna::util::ImageLoader::instance().getImage(simTile.coverUrl, luna::util::ImagePreset::VerticalPoster);
            if (sImg && sImg->ready) {
                drawPreScaledImage(sx, py, sImg->rgba.data(), posterW, posterH, sImg->width, sImg->height);
            } else {
                if (!simTile.coverUrl.empty()) luna::util::ImageLoader::instance().requestImage(simTile.coverUrl, luna::util::ImagePreset::VerticalPoster);
                drawRoundedRect(sx, py, posterW, posterH, 10, 0xFF241B3B);
            }
            if (detailZone == 2 && similarSelectedIdx == static_cast<int>(s)) {
                drawRoundedBorder(sx - 3, py - 3, posterW + 6, posterH + 6, 12, 3, 0xFFFFFFFF);
            }
        }
    }

    if (!statusText.empty()) {
        fillRect(0, 1046, 1920, 34, 0xEE0B0912);
        drawText(44, 1054, 1, statusText, 0xFF94A3B8);
    }

    present();
}

'''
c = between(c, 'void DisplayManager::drawGameDetailScreen(', 'void DisplayManager::drawNativeEngineWaitingScreen(', detail_new)

# 12. StartingSession with scene snapshot
start_new = r'''void DisplayManager::drawStartingSession(const GameTile& game,
                                         const std::string& statusMsg,
                                         uint64_t frameCount,
                                         bool cancelModalOpen,
                                         int cancelModalBtnIdx) {
    if (!isReady_ || !mappedMemory_) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);

    auto bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
    if (!bgImg || !bgImg->ready) {
        bgImg = luna::util::ImageLoader::instance().getImage(game.coverUrl, luna::util::ImagePreset::PanoramicDetail);
    }
    const bool bgReady = bgImg && bgImg->ready;
    if (!bgReady) {
        if (!game.backdropUrl.empty()) luna::util::ImageLoader::instance().requestImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
        else if (!game.coverUrl.empty()) luna::util::ImageLoader::instance().requestImage(game.coverUrl, luna::util::ImagePreset::PanoramicDetail);
    }
    std::shared_ptr<const luna::util::DecodedImage> logoImg;
    if (!game.logoUrl.empty()) {
        logoImg = luna::util::ImageLoader::instance().getImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
        if (!logoImg || !logoImg->ready) {
            luna::util::ImageLoader::instance().requestImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
            logoImg.reset();
        }
    }
    const std::string key = game.id + (bgReady ? "|B" : "|b") + (logoImg ? "L" : "l");

    auto drawStaticScene = [&]() {
        clear(0xFF07050A);
        if (bgReady) {
            drawPreScaledImage(0, 0, bgImg->rgba.data(), 1920, 1080, bgImg->width, bgImg->height);
        }
        blendRectUniform(0, 0, 1920, 1080, 0xFF07050A, 210);

        // PEGI top-left
        drawRoundedRect(68, 50, 60, 68, 8, 0xFFF59E0B);
        drawText(82, 58, 1, "PEGI", 0xFF0E0B16);
        drawText(78, 78, 3, "12", 0xFF0E0B16);

        // (X) cancel top-right
        drawRoundedRect(1650, 50, 200, 50, 10, 0xFF241B3B);
        drawRoundedBorder(1650, 50, 200, 50, 10, 1, 0xFF4C1D95);
        drawText(1680, 64, 2, "[(X) Cancelar]", 0xFFE2E8F0);

        if (logoImg) {
            int lx = (1920 - logoImg->width) / 2;
            drawPreScaledImage(lx, 380, logoImg->rgba.data(), logoImg->width, logoImg->height, logoImg->width, logoImg->height);
        } else {
            int tw = fontRenderer_.measureText(game.title, 48);
            drawText((1920 - tw) / 2, 430, 6, game.title, 0xFFFFFFFF);
        }
    };

#if defined(LUNA_TARGET_PS5)
    if (!sceneCache_.empty() && sceneKey_ == key) {
        std::memcpy(base, sceneCache_.data(), kUsedBytes);
    } else {
        drawStaticScene();
        sceneCache_.assign(base, base + kUsedBytes);
        sceneKey_ = key;
    }
#else
    drawStaticScene();
#endif

    // Dynamic part: 3 orbiting white dots + status message
    int spinX = 1480;
    int spinY = 960;
    int phase = static_cast<int>((frameCount / 6) % 3);
    for (int p = 0; p < 3; ++p) {
        uint32_t dotColor = (p == phase) ? 0xFFFFFFFF : 0xFF5B6475;
        int dotR = (p == phase) ? 7 : 5;
        drawRoundedRect(spinX + p * 24, spinY - dotR, dotR * 2, dotR * 2, dotR, dotColor);
    }
    std::string msg = statusMsg.empty() ? "Iniciando tu sesion de juego en la nube..." : statusMsg;
    drawText(spinX - 440, spinY - 6, 2, utf8SafeTruncate(msg, 48), 0xFFCBD5E1);

    if (cancelModalOpen) {
        blendRectUniform(0, 0, 1920, 1080, 0xFF000000, 0xCC);
        int mw = 840, mh = 320;
        int mx = (1920 - mw) / 2, my = (1080 - mh) / 2;
        drawRoundedRect(mx, my, mw, mh, 16, 0xFF181226);
        drawRoundedBorder(mx, my, mw, mh, 16, 2, 0xFF9146FF);
        drawText(mx + 50, my + 45, 4, "¿Salir del juego y abandonar la cola?", 0xFFFFFFFF);
        drawText(mx + 50, my + 105, 2, "Perderás tu posición actual en la cola de servidores de Amazon Luna.", 0xFFCBD5E1);
        drawPillButton(mx + 50, my + 180, 340, 60, "Continuar en la cola", cancelModalBtnIdx == 0, "", false);
        drawPillButton(mx + 420, my + 180, 340, 60, "Salir del juego", cancelModalBtnIdx == 1, "", false);
    }

    present();
}

'''
c = between(c, 'void DisplayManager::drawStartingSession(', 'void DisplayManager::drawPostSessionModal(', start_new)

# 13. Settings sections
c = rep(c, '''void DisplayManager::drawSettingsScreen(int selectedItemIdx,
                                        const std::string& statusText,
                                        uint64_t frameCount) {''', '''void DisplayManager::drawSettingsScreen(int selectedItemIdx,
                                        const std::string& statusText,
                                        uint64_t frameCount,
                                        int accountsSubTab) {''')
c = rep(c, '''    auto& s = luna::util::AppSettings::instance();

    // 10 Secciones oficiales de Ajustes''', '''    // 10 Secciones oficiales de Ajustes''')
for old, new in [
    ('{"Mandos y Gamepads",', '{"Mandos",'),
    ('{"Tu cuenta",', '{"Mi cuenta",'),
    ('{"Calidad de audio y video",', '{"Calidad de vídeo",'),
    ('{"Idioma y region",', '{"Idioma",'),
    ('{"Datos de partidas guardadas",', '{"Datos respaldados de juego",'),
    ('{"Ayuda y sugerencias",', '{"Ayuda y comentarios",'),
    ('{"Acerca de Luna y legal",', '{"Acerca de y aspectos jurídicos",'),
]:
    c = rep(c, old, new)

accounts_new = r'''} else if (selectedItemIdx == 1) {
        // Cuentas vinculadas con 2 subpestanas
        drawPillButton(pX + 40, dY - 10, 380, 50, "Cuentas vinculadas para jugar", accountsSubTab == 0, "", false);
        drawPillButton(pX + 440, dY - 10, 420, 50, "Cuentas vinculadas para reclamar juegos", accountsSubTab == 1, "", false);
        static const char* kPlayAccounts[] = {"EA", "EPIC GAMES", "UBISOFT", "TWITCH", "DISCORD"};
        static const char* kClaimAccounts[] = {"EA", "EPIC GAMES", "UBISOFT", "GOG", "PRIME GAMING"};
        const char** list = (accountsSubTab == 0) ? kPlayAccounts : kClaimAccounts;
        for (int a = 0; a < 5; ++a) {
            int ay = dY + 65 + a * 70;
            drawRoundedRect(pX + 40, ay, pW - 80, 58, 10, 0xFF1E1633);
            drawText(pX + 64, ay + 17, 2, list[a], 0xFFFFFFFF);
            drawText(pX + pW - 230, ay + 17, 2, "Vincular", 0xFFA78BFA);
        }
    } else if (selectedItemIdx == 2) {
        // Mi cuenta
        drawText(pX + 40, dY, 3, "Jesus (Bornicho #8663)", 0xFFFFFFFF);
        drawText(pX + 40, dY + 40, 2, "Plan actual: Luna Estándar (incluido con Prime)", 0xFF10B981);
        drawText(pX + 40, dY + 80, 2, "Gestiona tu cuenta de Amazon desde amazon.es/luna", 0xFFCBD5E1);
        drawPillButton(pX + 40, dY + 150, 380, 60, "Cerrar sesión de Amazon", true, "", false);
    '''
c = between(c, '} else if (selectedItemIdx == 1) {', '} else if (selectedItemIdx == 3) {', accounts_new)

lang_new = r'''} else if (selectedItemIdx == 5) {
        // Idioma: lista vertical de 13 idiomas con check
        drawText(pX + 40, dY - 10, 3, "Idioma de la aplicación", 0xFFFFFFFF);
        static const char* kLangs[13] = {
            "Español (España)", "English (United States)", "English (United Kingdom)",
            "Deutsch", "Français", "Italiano", "Polski", "Nederlands",
            "Português (Brasil)", "Español (Latinoamérica)", "Svenska", "Dansk", "Norsk"
        };
        const int activeLang = luna::util::I18n::getCurrentRegionIndex();
        for (int l = 0; l < 13; ++l) {
            int ly = dY + 30 + l * 50;
            bool act = (l == activeLang);
            drawRoundedRect(pX + 40, ly, pW - 80, 42, 8, act ? 0xFF3B1B68 : 0xFF1E1633);
            drawText(pX + 64, ly + 10, 2, kLangs[l], 0xFFFFFFFF);
            if (act) drawCheckGlyph(*this, pX + pW - 110, ly + 8, 22, 0xFF10B981);
        }
    '''
c = between(c, '} else if (selectedItemIdx == 5) {', '} else {\n        // Seccion general', lang_new)

# 14. Profile screens (new function) inserted before drawMediaViewerScreen
profile_new = r'''void DisplayManager::drawProfileScreen(int kind,
                                       int sel,
                                       int subTab,
                                       const PlayerProfileInfo& profile,
                                       const std::vector<GameTile>& games,
                                       uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;
    clear(0xFF0E0B16);

    fillRect(0, 0, 1920, 68, 0xFF110D1D);
    drawText(44, 22, 2, "<  Volver  (O)", 0xFFE2E8F0);

    static const int kFont[7] = {16, 16, 20, 26, 34, 44, 54};
    auto centerText = [&](int y, int scale, const std::string& t, uint32_t col) {
        int fs = kFont[std::clamp(scale, 0, 6)];
        int tw = fontRenderer_.measureText(t, fs);
        drawText((1920 - tw) / 2, y, scale, t, col);
    };
    const std::string dispName = profile.displayName.empty() ? std::string("Jesus") : profile.displayName;

    auto drawAvatar = [&](int cx, int cy, int d, uint32_t ring) {
        drawRoundedRect(cx - d / 2, cy - d / 2, d, d, d / 2, 0xFF14B8A6);
        if (!profile.avatarUrl.empty()) {
            auto img = luna::util::ImageLoader::instance().getImage(profile.avatarUrl, luna::util::ImagePreset::ProductLogo);
            if (img && img->ready) {
                drawPreScaledImage(cx - img->width / 2, cy - img->height / 2, img->rgba.data(), img->width, img->height, img->width, img->height);
            } else {
                luna::util::ImageLoader::instance().requestImage(profile.avatarUrl, luna::util::ImagePreset::ProductLogo);
            }
        }
        if (ring) drawRoundedBorder(cx - d / 2 - 6, cy - d / 2 - 6, d + 12, d + 12, d / 2 + 6, 4, ring);
    };
    auto drawPoster = [&](const GameTile& g, int x, int y, int w, int h) {
        auto img = luna::util::ImageLoader::instance().getImage(g.coverUrl, luna::util::ImagePreset::VerticalPoster);
        if (img && img->ready) {
            drawPreScaledImage(x, y, img->rgba.data(), w, h, img->width, img->height);
        } else {
            if (!g.coverUrl.empty()) luna::util::ImageLoader::instance().requestImage(g.coverUrl, luna::util::ImagePreset::VerticalPoster);
            drawRoundedRect(x, y, w, h, 10, 0xFF241B3B);
        }
    };

    if (kind == 0) {
        centerText(190, 6, "¿Quién va a jugar en Luna?", 0xFFFFFFFF);
        drawAvatar(790, 520, 200, sel == 0 ? 0xFFFFFFFF : 0);
        drawText(790 - fontRenderer_.measureText(dispName, 26) / 2, 650, 3, dispName, 0xFFFFFFFF);
        drawRoundedRect(740, 700, 100, 30, 15, 0xFF1D4ED8);
        drawText(758, 704, 1, "ACTUAL", 0xFFFFFFFF);
        drawRoundedRect(1030, 420, 200, 200, 100, 0xFF241B3B);
        if (sel == 1) drawRoundedBorder(1024, 414, 212, 212, 106, 4, 0xFFFFFFFF);
        drawText(1105, 478, 7, "+", 0xFFFFFFFF);
        drawText(1130 - fontRenderer_.measureText("Nuevo", 26) / 2, 650, 3, "Nuevo", 0xFFCBD5E1);
        drawPillButton(810, 820, 300, 60, "Editar perfiles", sel == 2, "", false);
    } else if (kind == 1) {
        drawText(72, 100, 6, "Planes Luna", 0xFFFFFFFF);
        const int cardY = 200, cardH = 760, cardW = 800;
        for (int k = 0; k < 2; ++k) {
            int cx = (k == 0) ? 150 : 970;
            bool foc = (sel == k);
            drawRoundedRect(cx, cardY, cardW, cardH, 20, 0xFF140F22);
            drawRoundedBorder(cx, cardY, cardW, cardH, 20, (k == 0 || foc) ? 4 : 2, (k == 0 || foc) ? 0xFFFFFFFF : 0xFF7C3AED);
            drawText(cx + 44, cardY + 40, 5, k == 0 ? "Luna Standard" : "Luna Premium", 0xFFFFFFFF);
            if (k == 0) {
                drawRoundedRect(cx + 44, cardY + 120, 300, 42, 21, 0xFF0B5FA5);
                drawCheckGlyph(*this, cx + 62, cardY + 130, 18, 0xFFFFFFFF);
                drawText(cx + 92, cardY + 131, 2, "Incluido con Prime", 0xFFFFFFFF);
            } else {
                drawRoundedRect(cx + 44, cardY + 120, 200, 42, 21, 0xFF7C3AED);
                drawText(cx + 66, cardY + 131, 2, "9,99 €/mes", 0xFFFFFFFF);
            }
            std::string desc = (k == 0)
                ? "Juega a una selección de juegos incluidos con tu suscripción a Amazon Prime, en streaming desde la nube."
                : "Más de 100 juegos en streaming con tu suscripción mensual. Sin descargas y en cualquier dispositivo.";
            auto lines = utf8WrapLines(desc, 58, 3);
            int ly = cardY + 196;
            for (const auto& l : lines) { drawText(cx + 44, ly, 2, l, 0xFFCBD5E1); ly += 28; }
            for (int i = 0; i < 3; ++i) {
                size_t gi = static_cast<size_t>(i + k * 3);
                if (gi < games.size()) drawPoster(games[gi], cx + 44 + i * 236, cardY + 400, 200, 280);
            }
        }
    } else if (kind == 2) {
        drawText(72, 100, 6, "Distintivos", 0xFFFFFFFF);
        drawPillButton(72, 200, 280, 54, "A continuación", subTab == 0, "", false);
        drawPillButton(372, 200, 280, 54, "Mis distintivos", subTab == 1, "", false);
        if (subTab == 0) {
            drawRoundedRect(72, 290, 1776, 280, 20, 0xFF3B1B68);
            drawText(120, 330, 5, "¡Bien hecho! Has desbloqueado un distintivo.", 0xFFFFFFFF);
            drawText(120, 430, 2, "Completados: 1/1", 0xFFE9D5FF);
            drawRoundedRect(120, 472, 560, 16, 8, 0xFF5B21B6);
            drawRoundedRect(120, 472, 560, 16, 8, 0xFFFCD34D);
            // Rocket illustration
            drawRoundedRect(1640, 316, 90, 190, 45, 0xFFE2E8F0);
            drawRoundedRect(1666, 372, 38, 38, 19, 0xFF3B82F6);
            fillRect(1618, 440, 26, 60, 0xFFEF4444);
            fillRect(1726, 440, 26, 60, 0xFFEF4444);
            drawRoundedRect(1668, 506, 34, 46, 17, 0xFFF59E0B);
            drawText(72, 610, 4, "Sigue desbloqueando distintivos en GameNight", 0xFFFFFFFF);
            for (int i = 0; i < 7; ++i) {
                if (static_cast<size_t>(i) < games.size()) drawPoster(games[i], 72 + i * 248, 670, 200, 280);
            }
        } else {
            const int cellW = 92, cellH = 104, gap = 14;
            for (int i = 0; i < 24; ++i) {
                int cx = 72 + (i % 8) * (cellW + gap);
                int cy = 300 + (i / 8) * (cellH + gap);
                bool unlocked = (i == 0);
                drawRoundedRect(cx, cy, cellW, cellH, 14, unlocked ? 0xFF7C3AED : 0xFF1C152E);
                if (sel == i) drawRoundedBorder(cx - 3, cy - 3, cellW + 6, cellH + 6, 16, 3, 0xFFFFFFFF);
                if (unlocked) {
                    drawStarIcon(cx + cellW / 2, cy + cellH / 2, 22, 0xFFFCD34D);
                } else {
                    fillRect(cx + 34, cy + 50, 24, 20, 0xFF64748B);
                    drawRoundedBorder(cx + 37, cy + 34, 18, 20, 8, 3, 0xFF64748B);
                }
            }
            drawRoundedRect(1000, 300, 848, 560, 20, 0xFF140F22);
            drawRoundedBorder(1000, 300, 848, 560, 20, 1, 0xFF2D2248);
            bool unl = (sel == 0);
            drawRoundedRect(1330, 340, 190, 220, 30, unl ? 0xFF7C3AED : 0xFF1C152E);
            if (unl) {
                drawStarIcon(1425, 450, 60, 0xFFFCD34D);
            } else {
                fillRect(1400, 470, 50, 40, 0xFF64748B);
                drawRoundedBorder(1405, 430, 40, 42, 18, 5, 0xFF64748B);
            }
            std::string nm = unl ? "Primer lanzamiento" : "Próximamente";
            drawText(1424 - fontRenderer_.measureText(nm, 34) / 2, 600, 4, nm, 0xFFFFFFFF);
            std::string ds = unl ? "Inicia tu primera partida en Amazon Luna." : "Este distintivo estará disponible próximamente.";
            drawText(1424 - fontRenderer_.measureText(ds, 20) / 2, 660, 2, ds, 0xFFCBD5E1);
        }
    } else {
        centerText(150, 6, "Stream", 0xFFFFFFFF);
        drawLunaLogoIcon(800, 262, 40);
        drawText(850, 262, 4, "luna", 0xFFFFFFFF);
        drawText(965, 262, 4, "|", 0xFF64748B);
        drawText(1000, 262, 4, "twitch", 0xFF9146FF);
        centerText(360, 4, "Te has conectado a Twitch", 0xFFFFFFFF);
        drawRoundedRect(890, 450, 140, 140, 70, 0xFF9146FF);
        drawText(960 - fontRenderer_.measureText("B", 54) / 2, 478, 6, "B", 0xFFFFFFFF);
        centerText(610, 3, "bornicho", 0xFFE2E8F0);
        drawPillButton(770, 700, 380, 60, "Desvincular cuenta", sel == 0, "", false);
        drawRoundedRect(460, 810, 1000, 70, 35, 0xFFFFFFFF);
        if (sel == 1) drawRoundedBorder(456, 806, 1008, 78, 39, 4, 0xFF9146FF);
        centerText(832, 3, "Configura el stream", 0xFF0E0B16);
    }

    present();
}

'''
c = rep(c, 'void DisplayManager::drawMediaViewerScreen(', profile_new + 'void DisplayManager::drawMediaViewerScreen(')

# 15. Media viewer thumbnails: use the 16:9 card preset (already requested by detail screen)
c = c.replace('luna::util::ImagePreset::ScreenshotThumbnail', 'luna::util::ImagePreset::HorizontalCard16_9')

save(C, c)
print('display patched OK')
