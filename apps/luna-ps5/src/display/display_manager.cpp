#include "display/display_manager.h"
#include "display/clay_renderer.h"
#include "display/luna_clay_ui.h"
#include "media/mp4_preview_player.h"
#include "util/settings.h"
#include "util/i18n.h"
#include "display/hero_banner_asset.h"
#include "util/image_loader.h"
#include "util/image_loader.h"
#include <cmath>
#include "util/crash_logger.h"
#include "util/qrcodegen.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <array>

#if defined(LUNA_TARGET_PS5)
extern "C" {
int sceSystemServiceHideSplashScreen(void);
size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end,
                                  size_t length, size_t alignment, int memory_type,
                                  int64_t *physical_address);
int sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags,
                             int64_t physical_address, size_t alignment);
int sceVideoOutOpen(int32_t user_id, int32_t bus_type, int32_t index, const void *param);
int sceVideoOutClose(int32_t handle);
int sceVideoOutSetFlipRate(int32_t handle, int32_t rate);
void sceVideoOutSetBufferAttribute2(void *attribute, uint64_t pixel_format,
                                    uint32_t tiling_mode, uint32_t width,
                                    uint32_t height, uint64_t option,
                                    uint32_t dcc_control, uint64_t dcc_clear_color);
int sceVideoOutRegisterBuffers2(int32_t handle, int32_t set_index,
                                int32_t buffer_index_start, void *buffers,
                                int32_t buffer_count, void *attribute,
                                int32_t category, void *option);
int sceVideoOutSubmitFlip(int32_t handle, int32_t buffer_index,
                          uint32_t flip_mode, int64_t flip_argument);
int sceVideoOutWaitVblank(int32_t handle);
}
#endif

namespace luna::display {

namespace {

constexpr unsigned kWidth = 1920;
constexpr unsigned kHeight = 1080;
constexpr unsigned kTilesX = (kWidth + 127) / 128;  // 15 macro-tiles
constexpr unsigned kTilesY = (kHeight + 127) / 128; // 9 macro-tiles
constexpr size_t kUsedBytes = static_cast<size_t>(kTilesX) * kTilesY * 0x10000;
constexpr size_t kFrameBytes = 0xA00000; // 10 MB por buffer (alineado a 2 MB)
constexpr int kBuffers = 3;              // Triple buffering
constexpr size_t kMemoryBytes = kFrameBytes * kBuffers;
constexpr size_t kMemoryAlignment = 0x200000;
constexpr int kMemoryTypeGarlic = 3;
constexpr int kMapProtection = 0x33;
constexpr uint64_t kPixelFormatRGBA8 = 0x8000000022000000ULL;

// Linear canvas buffer in .bss (8.29 MB, 0 bytes in malloc heap)
alignas(64) static uint32_t g_canvasBuffer[kWidth * kHeight];

struct VideoBufferDesc {
    void* data;
    void* metadata;
    void* res0;
    void* res1;
};

// Mapeo swizzled oficial de macro-tiles de 64KB para sceVideoOut de PS5
constexpr size_t tiledByteOffset(unsigned x, unsigned y) {
    uint32_t offset = ((y << 4) & 0x70U) ^ ((y << 5) & 0xf00U) ^ ((y << 9) & 0x1000U) ^
                      ((y << 8) & 0x4000U) ^ ((x << 2) & 0xcU) ^ ((x << 5) & 0x380U) ^
                      ((x << 4) & 0x400U) ^ ((x << 6) & 0x800U) ^ ((x << 9) & 0xa000U);
    uint32_t blocksPerRow = (kWidth + 127U) >> 7;
    uint32_t blockIndex = (y >> 7) * blocksPerRow + (x >> 7);
    return (static_cast<size_t>(blockIndex) << 16) + offset;
}

struct TileOrder {
    std::array<uint8_t, 16384> dx{}, dy{};
    TileOrder() {
        for (unsigned y = 0; y < 128; ++y) {
            for (unsigned x = 0; x < 128; ++x) {
                size_t word = tiledByteOffset(x, y) / 4;
                dx[word] = static_cast<uint8_t>(x);
                dy[word] = static_cast<uint8_t>(y);
            }
        }
    }
};

const TileOrder& getTileOrder() {
    static const TileOrder order;
    return order;
}

// Precomputed per-column / per-row XOR terms of the macro-tile swizzle (avoids recomputing 9 shifts per pixel)
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

// Convierte ARGB (0xAARRGGBB) a ABGR (0xAABBGGRR) para memoria RGBA8 de PS5
constexpr uint32_t argbToAbgr(uint32_t c) {
    uint32_t a = (c >> 24) & 0xFF;
    uint32_t r = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = c & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}



#if defined(LUNA_TARGET_PS5)
void flushCacheRange(void* address, size_t length) {
    auto* at = static_cast<uint8_t*>(address);
    const auto* end = at + length;
    for (; at < end; at += 64) {
        __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    }
    __asm__ volatile("mfence" ::: "memory");
}
#endif

inline uint8_t clamp8(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

} // namespace

DisplayManager::DisplayManager() = default;

DisplayManager::~DisplayManager() {
    shutdown();
}

bool DisplayManager::init(int width, int height) {
    width_ = width;
    height_ = height;

#if defined(LUNA_TARGET_PS5)
    sceSystemServiceHideSplashScreen();

    videoHandle_ = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (videoHandle_ < 0) {
        std::printf("[Display] Error al abrir sceVideoOut: %d\n", videoHandle_);
        return false;
    }

    size_t poolSize = sceKernelGetDirectMemorySize();
    if (poolSize < kMemoryBytes) {
        std::printf("[Display] Memoria directa insuficiente: %zu bytes\n", poolSize);
        return false;
    }

    int64_t physAddr = 0;
    int rc = sceKernelAllocateDirectMemory(0, static_cast<int64_t>(poolSize), kMemoryBytes,
                                           kMemoryAlignment, kMemoryTypeGarlic, &physAddr);
    if (rc < 0) {
        std::printf("[Display] Error sceKernelAllocateDirectMemory: 0x%08X\n", rc);
        return false;
    }

    rc = sceKernelMapDirectMemory(&mappedMemory_, kMemoryBytes, kMapProtection, 0, physAddr, kMemoryAlignment);
    if (rc < 0 || !mappedMemory_) {
        std::printf("[Display] Error sceKernelMapDirectMemory: 0x%08X\n", rc);
        return false;
    }

    VideoBufferDesc buffers[kBuffers]{};
    for (int i = 0; i < kBuffers; ++i) {
        buffers[i].data = static_cast<uint8_t*>(mappedMemory_) + (i * kFrameBytes);
    }

    uint8_t attr[80]{};
    sceVideoOutSetFlipRate(videoHandle_, 0); // 60 FPS
    sceVideoOutSetBufferAttribute2(attr, kPixelFormatRGBA8, 0, kWidth, kHeight, 0, 0, 0);

    rc = sceVideoOutRegisterBuffers2(videoHandle_, 0, 0, buffers, kBuffers, attr, 0, nullptr);
    if (rc < 0) {
        std::printf("[Display] Error sceVideoOutRegisterBuffers2: 0x%08X\n", rc);
        return false;
    }

    std::printf("[Display] Pantalla PS5 inicializada (%dx%d, 64KB Macro-Tiled Triple-Buffer)\n", width_, height_);
#else
    mappedMemory_ = std::malloc(kMemoryBytes);
    std::printf("[Display] Pantalla Desktop inicializada (%dx%d)\n", width_, height_);
#endif

    isReady_ = true;
    canvas_ = g_canvasBuffer;
    std::fill_n(canvas_, width_ * height_, 0xFF000000U);
    fontRenderer_.init();
    loadStaticHeroBanner();
    ClayRenderer::instance().init(this, width_, height_);
    return true;
}

void DisplayManager::shutdown() {
    if (isReady_) {
#if defined(LUNA_TARGET_PS5)
        if (videoHandle_ >= 0) {
            sceVideoOutClose(videoHandle_);
            videoHandle_ = -1;
        }
#else
        if (mappedMemory_) {
            std::free(mappedMemory_);
            mappedMemory_ = nullptr;
        }
#endif
        isReady_ = false;
        std::printf("[Display] Subsistema de pantalla cerrado\n");
    }
}

void DisplayManager::loadStaticHeroBanner() {
    if (!heroBannerRgba_.empty()) return;
    int w = 0, h = 0;
    if (luna::util::decodeImageRgba(luna::assets::kLunaPremiumHeroJpg,
                                   luna::assets::kLunaPremiumHeroJpgSize,
                                   heroBannerRgba_, w, h)) {
        heroBannerWidth_ = w;
        heroBannerHeight_ = h;
        std::printf("[Display] Static Hero Banner decodificado con exito (%dx%d, %zu bytes RGBA)\n",
                    w, h, heroBannerRgba_.size());
    }
}

void DisplayManager::putPixel(int x, int y, uint32_t colorArgb) {
    if (x >= clipRect_.x0 && x < clipRect_.x1 &&
        y >= clipRect_.y0 && y < clipRect_.y1 &&
        static_cast<unsigned>(x) < static_cast<unsigned>(width_) &&
        static_cast<unsigned>(y) < static_cast<unsigned>(height_)) {
        canvas_[y * width_ + x] = argbToAbgr(colorArgb);
    }
}

uint32_t DisplayManager::getPixel(int x, int y) const {
    if (static_cast<unsigned>(x) < static_cast<unsigned>(width_) &&
        static_cast<unsigned>(y) < static_cast<unsigned>(height_)) {
        return canvas_[y * width_ + x];
    }
    return 0;
}

void DisplayManager::blendPixel(int x, int y, uint32_t colorArgb, uint8_t alpha) {
    if (x < clipRect_.x0 || x >= clipRect_.x1 ||
        y < clipRect_.y0 || y >= clipRect_.y1 ||
        static_cast<unsigned>(x) >= static_cast<unsigned>(width_) ||
        static_cast<unsigned>(y) >= static_cast<unsigned>(height_) || alpha == 0) return;

    uint32_t srcA = (((colorArgb >> 24) & 0xFF) * alpha) / 255;
    if (srcA == 0) return;
    size_t idx = static_cast<size_t>(y) * width_ + x;
    if (srcA >= 255) {
        canvas_[idx] = argbToAbgr(colorArgb);
        return;
    }

    uint32_t dstAbgr = canvas_[idx];
    uint32_t dstR = dstAbgr & 0xFF;
    uint32_t dstG = (dstAbgr >> 8) & 0xFF;
    uint32_t dstB = (dstAbgr >> 16) & 0xFF;

    uint32_t srcR = (colorArgb >> 16) & 0xFF;
    uint32_t srcG = (colorArgb >> 8) & 0xFF;
    uint32_t srcB = colorArgb & 0xFF;

    uint32_t invA = 255 - srcA;
    uint32_t outR = (srcR * srcA + dstR * invA) / 255;
    uint32_t outG = (srcG * srcA + dstG * invA) / 255;
    uint32_t outB = (srcB * srcA + dstB * invA) / 255;
    canvas_[idx] = 0xFF000000U | (outB << 16) | (outG << 8) | outR;
}

void DisplayManager::pushScissor(int x, int y, int w, int h) {
    clipStack_.push_back(clipRect_);
    int nx0 = std::max(clipRect_.x0, x);
    int ny0 = std::max(clipRect_.y0, y);
    int nx1 = std::min(clipRect_.x1, x + w);
    int ny1 = std::min(clipRect_.y1, y + h);
    clipRect_ = { nx0, ny0, std::max(nx0, nx1), std::max(ny0, ny1) };
}

void DisplayManager::popScissor() {
    if (!clipStack_.empty()) {
        clipRect_ = clipStack_.back();
        clipStack_.pop_back();
    } else {
        clipRect_ = { 0, 0, width_, height_ };
    }
}

void DisplayManager::resetScissor() {
    clipStack_.clear();
    clipRect_ = { 0, 0, width_, height_ };
}

void DisplayManager::clear(uint32_t colorArgb) {
    if (!isReady_ || !canvas_) return;
    uint32_t abgr = argbToAbgr(colorArgb);
    std::fill_n(canvas_, width_ * height_, abgr);
}

void DisplayManager::fillRect(int rx, int ry, int rw, int rh, uint32_t colorArgb) {
    if (!isReady_ || rw <= 0 || rh <= 0) return;
    int x0 = std::max({0, rx, clipRect_.x0});
    int y0 = std::max({0, ry, clipRect_.y0});
    int x1 = std::min({width_, rx + rw, clipRect_.x1});
    int y1 = std::min({height_, ry + rh, clipRect_.y1});
    if (x0 >= x1 || y0 >= y1) return;

    uint32_t abgr = argbToAbgr(colorArgb);
    int count = x1 - x0;
    for (int y = y0; y < y1; ++y) {
        std::fill_n(&canvas_[y * width_ + x0], count, abgr);
    }
}

void DisplayManager::drawImage(int rx, int ry, int rw, int rh, const uint8_t* rgba, int imgW, int imgH) {
    drawPreScaledImage(rx, ry, rgba, rw, rh, imgW, imgH);
}

void DisplayManager::drawPreScaledImage(int rx, int ry, const uint8_t* rgba, int dstW, int dstH, int srcW, int srcH) {
    if (!isReady_ || !rgba || dstW <= 0 || dstH <= 0) return;
    if (srcW <= 0) srcW = dstW;
    if (srcH <= 0) srcH = dstH;
    if (srcW > 4096 || srcH > 4096) return;

    int x0 = std::clamp(rx, std::max(0, clipRect_.x0), std::min(width_, clipRect_.x1));
    int y0 = std::clamp(ry, std::max(0, clipRect_.y0), std::min(height_, clipRect_.y1));
    int x1 = std::clamp(rx + dstW, std::max(0, clipRect_.x0), std::min(width_, clipRect_.x1));
    int y1 = std::clamp(ry + dstH, std::max(0, clipRect_.y0), std::min(height_, clipRect_.y1));
    if (x0 >= x1 || y0 >= y1) return;

    const bool oneToOne = (srcW == dstW && srcH == dstH);
    static int xmap[1920 + 128];
    if (!oneToOne) {
        for (int x = x0; x < x1; ++x) {
            int sx = static_cast<int>((static_cast<int64_t>(x - rx) * srcW) / dstW);
            xmap[x] = (sx >= srcW) ? (srcW - 1) : sx;
        }
    }

    for (int y = y0; y < y1; ++y) {
        int srcY = oneToOne ? (y - ry) : static_cast<int>((static_cast<int64_t>(y - ry) * srcH) / dstH);
        if (srcY >= srcH) srcY = srcH - 1;
        const uint8_t* rowSrc = rgba + static_cast<size_t>(srcY) * static_cast<size_t>(srcW) * 4;
        uint32_t* dstRow = &canvas_[y * width_];

        for (int x = x0; x < x1; ++x) {
            const int sx = oneToOne ? (x - rx) : xmap[x];
            const uint8_t* p = rowSrc + static_cast<size_t>(sx) * 4;
            const uint8_t a = p[3];
            if (a == 0) continue;
            if (a == 255) {
                uint32_t px;
                std::memcpy(&px, p, 4); // RGBA bytes on little endian = ABGR word expected by the PS5 VideoOut framebuffer
                dstRow[x] = px;
            } else {
                const uint32_t inv = 255U - a;
                const uint32_t d = dstRow[x];
                const uint32_t r = (p[0] * a + (d & 0xFF) * inv) / 255U;
                const uint32_t g = (p[1] * a + ((d >> 8) & 0xFF) * inv) / 255U;
                const uint32_t b = (p[2] * a + ((d >> 16) & 0xFF) * inv) / 255U;
                dstRow[x] = 0xFF000000U | (b << 16) | (g << 8) | r;
            }
        }
    }
}

void DisplayManager::blendRectUniform(int rx, int ry, int rw, int rh, uint32_t colorArgb, uint8_t alpha) {
    if (!isReady_ || alpha == 0) return;
    int x0 = std::max(0, rx);
    int y0 = std::max(0, ry);
    int x1 = std::min(width_, rx + rw);
    int y1 = std::min(height_, ry + rh);
    if (x0 >= x1 || y0 >= y1) return;

    const uint32_t pr = ((colorArgb >> 16) & 0xFF) * alpha;
    const uint32_t pg = ((colorArgb >> 8) & 0xFF) * alpha;
    const uint32_t pb = (colorArgb & 0xFF) * alpha;
    const uint32_t inv = 255U - alpha;

    for (int y = y0; y < y1; ++y) {
        uint32_t* row = &canvas_[y * width_];
        for (int x = x0; x < x1; ++x) {
            uint32_t d = row[x];
            uint32_t r = (pr + (d & 0xFF) * inv) / 255U;
            uint32_t g = (pg + ((d >> 8) & 0xFF) * inv) / 255U;
            uint32_t b = (pb + ((d >> 16) & 0xFF) * inv) / 255U;
            row[x] = 0xFF000000U | (b << 16) | (g << 8) | r;
        }
    }
}



void DisplayManager::drawPlayTriangleIcon(int cx, int cy, int size, uint32_t color) {
    if (!isReady_ || size <= 0) return;
    int left = cx - size * 2 / 3;
    int right = cx + size * 4 / 3;
    int span = right - left;
    if (span <= 0) return;

    for (int x = left; x <= right; ++x) {
        if (x < 0 || x >= width_) continue;
        float frac = static_cast<float>(right - x) / static_cast<float>(span);
        int halfH = static_cast<int>(std::round(static_cast<float>(size) * frac));
        int y0 = std::max(0, cy - halfH);
        int y1 = std::min(height_ - 1, cy + halfH);
        for (int y = y0; y <= y1; ++y) {
            putPixel(x, y, color);
        }
    }
}

void DisplayManager::drawStarIcon(int cx, int cy, int size, uint32_t color) {
    if (!isReady_ || size <= 0) return;
    float rOuter = static_cast<float>(size);
    float rInner = rOuter * 0.45f;
    int minX = std::max(0, static_cast<int>(cx - rOuter - 1));
    int maxX = std::min(width_ - 1, static_cast<int>(cx + rOuter + 1));
    int minY = std::max(0, static_cast<int>(cy - rOuter - 1));
    int maxY = std::min(height_ - 1, static_cast<int>(cy + rOuter + 1));

    float vx[10], vy[10];
    for (int i = 0; i < 10; ++i) {
        float angle = -1.5707963f + static_cast<float>(i) * 0.62831853f;
        float r = (i % 2 == 0) ? rOuter : rInner;
        vx[i] = static_cast<float>(cx) + r * std::cos(angle);
        vy[i] = static_cast<float>(cy) + r * std::sin(angle);
    }

    for (int y = minY; y <= maxY; ++y) {
        float py = static_cast<float>(y);
        for (int x = minX; x <= maxX; ++x) {
            float px = static_cast<float>(x);
            bool inside = false;
            for (int i = 0, j = 9; i < 10; j = i++) {
                if (((vy[i] > py) != (vy[j] > py)) &&
                    (px < (vx[j] - vx[i]) * (py - vy[i]) / (vy[j] - vy[i]) + vx[i])) {
                    inside = !inside;
                }
            }
            if (inside) {
                putPixel(x, y, color);
            }
        }
    }
}

void DisplayManager::drawLunaLogoIcon(int x, int y, int size) {
    if (!isReady_ || size <= 0) return;
    int s2 = size * 2 / 3;
    drawRoundedRect(x, y + 4, s2, s2, 5, 0xFF02FDDB);
    drawRoundedRect(x + size / 3, y, s2, s2, 5, 0xFF9146FF);
}

void DisplayManager::drawMutedSpeakerIcon(int cx, int cy, int size, uint32_t color) {
    if (!isReady_ || size <= 0) return;
    int half = size / 2;
    fillRect(cx - half, cy - half / 2, half * 2 / 3, half, color);
    for (int y = -half; y <= half; ++y) {
        int xSpan = (half - std::abs(y));
        if (xSpan > 0) {
            fillRect(cx - half / 3, cy + y, xSpan, 1, color);
        }
    }
    int x0 = cx + half / 2;
    for (int d = -4; d <= 4; ++d) {
        putPixel(x0 + d, cy + d, color);
        putPixel(x0 + d + 1, cy + d, color);
        putPixel(x0 + d, cy - d, color);
        putPixel(x0 + d + 1, cy - d, color);
    }
}

void DisplayManager::drawPegiBadge(int x, int y, int w, int h, const std::string& rating) {
    if (!isReady_ || w <= 0 || h <= 0) return;
    std::string num = "12";
    if (rating.find("18") != std::string::npos) num = "18";
    else if (rating.find("16") != std::string::npos) num = "16";
    else if (rating.find("7") != std::string::npos) num = "7";
    else if (rating.find("3") != std::string::npos) num = "3";

    uint32_t pegiBg = 0xFFF58220; // Default orange (12, 16)
    if (num == "3" || num == "7") pegiBg = 0xFF65B32E; // Green
    else if (num == "18") pegiBg = 0xFFE30613; // Red

    drawRoundedRect(x, y, w, h, 6, pegiBg);

    int tw = fontRenderer_.measureText(num, 24);
    int tx = x + (w - tw) / 2;
    drawText(tx, y + 8, 3, num, 0xFFFFFFFF);

    int stripeH = 14;
    int stripeY = y + h - stripeH;
    fillRect(x, stripeY, w, stripeH, 0xFFFFFFFF);
    drawText(x + 3, stripeY + 2, 1, "pegi.info", 0xFF000000);
}

void DisplayManager::drawCategoryIconTinted(int dx, int dy, const uint8_t* rgba, int dstW, int dstH, int srcW, int srcH, uint32_t color) {
    if (!isReady_ || !rgba || dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0) return;
    for (int y = 0; y < dstH; ++y) {
        int srcY = (y * srcH) / dstH;
        const uint8_t* rowSrc = rgba + static_cast<size_t>(srcY) * static_cast<size_t>(srcW) * 4;
        for (int x = 0; x < dstW; ++x) {
            int srcX = (x * srcW) / dstW;
            const uint8_t* p = rowSrc + static_cast<size_t>(srcX) * 4;
            uint8_t a = p[3];
            if (a > 20) {
                blendPixel(dx + x, dy + y, color, a);
            }
        }
    }
}

void DisplayManager::drawCategoryIcon(int cx, int cy, int size, const std::string& catName, uint32_t color) {
    if (!isReady_ || size <= 0) return;
    int r = size / 2;

    std::string lower = catName;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.find("todos") != std::string::npos || lower.find("all") != std::string::npos) {
        int sq = 12;
        int g = 4;
        drawRoundedRect(cx - sq - g/2, cy - sq - g/2, sq, sq, 3, color);
        drawRoundedRect(cx + g/2, cy - sq - g/2, sq, sq, 3, color);
        drawRoundedRect(cx - sq - g/2, cy + g/2, sq, sq, 3, color);
        drawRoundedRect(cx + g/2, cy + g/2, sq, sq, 3, color);
    } else if (lower.find("acci") != std::string::npos || lower.find("action") != std::string::npos) {
        for (int y = -r + 4; y <= -r + 14; ++y) {
            int span = y - (-r + 4);
            fillRect(cx - span, cy + y, span * 2 + 1, 1, color);
        }
        fillRect(cx - 5, cy - r + 14, 11, 14, color);
        for (int i = 0; i < 6; ++i) {
            fillRect(cx - 10 + i, cy + 2 + i, 2, 6, color);
            fillRect(cx + 9 - i, cy + 2 + i, 2, 6, color);
        }
    } else if (lower.find("aventura") != std::string::npos || lower.find("adventure") != std::string::npos) {
        for (int i = 0; i < 18; ++i) {
            fillRect(cx - 6 - i, cy + 12 - i, i * 2 + 1, 1, color);
        }
        for (int i = 0; i < 12; ++i) {
            fillRect(cx + 8 - i, cy + 12 - i, i * 2 + 1, 1, color);
        }
    } else if (lower.find("arcade") != std::string::npos) {
        drawRoundedRect(cx - 10, cy + 8, 20, 6, 2, color);
        fillRect(cx - 2, cy - 4, 4, 12, color);
        drawRoundedRect(cx - 7, cy - 14, 14, 14, 7, color);
    } else if (lower.find("lucha") != std::string::npos || lower.find("fight") != std::string::npos) {
        drawRoundedRect(cx - 10, cy - 12, 18, 18, 6, color);
        drawRoundedRect(cx + 4, cy - 4, 8, 8, 3, color);
        fillRect(cx - 10, cy + 6, 20, 6, color);
    } else if (lower.find("terror") != std::string::npos || lower.find("horror") != std::string::npos) {
        drawRoundedRect(cx - 11, cy - 12, 22, 16, 8, color);
        fillRect(cx - 6, cy + 4, 12, 6, color);
        fillRect(cx - 6, cy - 4, 4, 4, 0xFF211D30);
        fillRect(cx + 2, cy - 4, 4, 4, 0xFF211D30);
    } else if (lower.find("fiesta") != std::string::npos || lower.find("party") != std::string::npos) {
        for (int i = 0; i < 18; ++i) {
            fillRect(cx - i / 2, cy - 12 + i, i + 1, 1, color);
        }
        fillRect(cx - 10, cy - 6, 3, 3, color);
        fillRect(cx + 10, cy - 4, 3, 3, color);
        fillRect(cx + 8, cy - 12, 3, 3, color);
    } else if (lower.find("gica") != std::string::npos || lower.find("puzzle") != std::string::npos) {
        drawRoundedRect(cx - 10, cy - 10, 20, 20, 3, color);
        drawRoundedRect(cx - 4, cy - 15, 8, 7, 3, color);
        drawRoundedRect(cx + 8, cy - 4, 7, 8, 3, color);
    } else if (lower.find("carreras") != std::string::npos || lower.find("racing") != std::string::npos) {
        fillRect(cx - 12, cy - 12, 3, 26, color);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                if ((row + col) % 2 == 0) {
                    fillRect(cx - 9 + col * 5, cy - 12 + row * 5, 5, 5, color);
                }
            }
        }
    } else if (lower.find("rol") != std::string::npos || lower.find("rpg") != std::string::npos) {
        for (int d = -10; d <= 10; ++d) {
            putPixel(cx + d, cy + d, color);
            putPixel(cx + d + 1, cy + d, color);
            putPixel(cx + d, cy - d, color);
            putPixel(cx + d + 1, cy - d, color);
        }
        fillRect(cx - 8, cy - 5, 4, 4, color);
        fillRect(cx + 4, cy - 5, 4, 4, color);
    } else if (lower.find("disparos") != std::string::npos || lower.find("shooter") != std::string::npos) {
        drawRoundedBorder(cx - 12, cy - 12, 24, 24, 12, 2, color);
        fillRect(cx - 16, cy, 7, 2, color);
        fillRect(cx + 9, cy, 7, 2, color);
        fillRect(cx, cy - 16, 2, 7, color);
        fillRect(cx, cy + 9, 2, 7, color);
        fillRect(cx - 1, cy - 1, 2, 2, color);
    } else if (lower.find("simula") != std::string::npos) {
        drawRoundedRect(cx - 14, cy - 8, 28, 16, 6, color);
        fillRect(cx - 10, cy - 1, 6, 2, 0xFF211D30);
        fillRect(cx - 8, cy - 3, 2, 6, 0xFF211D30);
        putPixel(cx + 8, cy - 2, 0xFF211D30);
        putPixel(cx + 6, cy, 0xFF211D30);
    } else if (lower.find("deporte") != std::string::npos || lower.find("sport") != std::string::npos) {
        drawRoundedRect(cx - 12, cy - 12, 24, 24, 12, color);
        drawRoundedRect(cx - 10, cy - 10, 20, 20, 10, 0xFF211D30);
        drawRoundedRect(cx - 4, cy - 4, 8, 8, 3, color);
    } else if (lower.find("estrategia") != std::string::npos || lower.find("strategy") != std::string::npos) {
        drawRoundedRect(cx - 10, cy + 8, 20, 5, 2, color);
        fillRect(cx - 6, cy - 6, 12, 14, color);
        fillRect(cx - 8, cy - 12, 16, 6, color);
        fillRect(cx - 4, cy - 12, 3, 3, 0xFF211D30);
        fillRect(cx + 1, cy - 12, 3, 3, 0xFF211D30);
    } else {
        drawRoundedRect(cx - 10, cy - 10, 20, 20, 4, color);
        fillRect(cx - 6, cy - 6, 3, 3, 0xFF211D30);
        fillRect(cx + 3, cy - 6, 3, 3, 0xFF211D30);
        fillRect(cx - 1, cy - 1, 3, 3, 0xFF211D30);
        fillRect(cx - 6, cy + 3, 3, 3, 0xFF211D30);
        fillRect(cx + 3, cy + 3, 3, 3, 0xFF211D30);
    }
}

void DisplayManager::drawRoundedRect(int x, int y, int w, int h, int r, uint32_t colorArgb) {
    if (!isReady_ || w <= 0 || h <= 0) return;
    if (r <= 0) {
        fillRect(x, y, w, h, colorArgb);
        return;
    }
    r = std::min(r, std::min(w / 2, h / 2));
    fillRect(x, y + r, w, h - (2 * r), colorArgb);
    fillRect(x + r, y, w - (2 * r), r, colorArgb);
    fillRect(x + r, y + h - r, w - (2 * r), r, colorArgb);

    float rf = static_cast<float>(r);
    for (int cy = 0; cy < r; ++cy) {
        float dy = rf - 0.5f - static_cast<float>(cy);
        for (int cx = 0; cx < r; ++cx) {
            float dx = rf - 0.5f - static_cast<float>(cx);
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= rf) {
                uint8_t a = 255;
                if (dist > rf - 1.0f) {
                    a = static_cast<uint8_t>(255 * (1.0f - (dist - (rf - 1.0f))));
                }
                blendPixel(x + cx, y + cy, colorArgb, a);
                blendPixel(x + w - 1 - cx, y + cy, colorArgb, a);
                blendPixel(x + cx, y + h - 1 - cy, colorArgb, a);
                blendPixel(x + w - 1 - cx, y + h - 1 - cy, colorArgb, a);
            }
        }
    }
}

void DisplayManager::drawRoundedBorder(int x, int y, int w, int h, int r, int thickness, uint32_t colorArgb) {
    if (!isReady_ || thickness <= 0 || w <= 0 || h <= 0) return;
    r = std::min(r, std::min(w / 2, h / 2));
    float rOuter = static_cast<float>(r);
    float rInner = std::max(0.0f, rOuter - static_cast<float>(thickness));

    fillRect(x + r, y, w - (2 * r), thickness, colorArgb);
    fillRect(x + r, y + h - thickness, w - (2 * r), thickness, colorArgb);
    fillRect(x, y + r, thickness, h - (2 * r), colorArgb);
    fillRect(x + w - thickness, y + r, thickness, h - (2 * r), colorArgb);

    for (int cy = 0; cy < r; ++cy) {
        float dy = rOuter - 0.5f - static_cast<float>(cy);
        for (int cx = 0; cx < r; ++cx) {
            float dx = rOuter - 0.5f - static_cast<float>(cx);
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= rOuter && dist >= rInner) {
                uint8_t a = 255;
                if (dist > rOuter - 1.0f) {
                    a = static_cast<uint8_t>(255 * (1.0f - (dist - (rOuter - 1.0f))));
                } else if (dist < rInner + 1.0f && rInner > 0.0f) {
                    a = static_cast<uint8_t>(255 * (dist - rInner));
                }
                blendPixel(x + cx, y + cy, colorArgb, a);
                blendPixel(x + w - 1 - cx, y + cy, colorArgb, a);
                blendPixel(x + cx, y + h - 1 - cy, colorArgb, a);
                blendPixel(x + w - 1 - cx, y + h - 1 - cy, colorArgb, a);
            }
        }
    }
}

void DisplayManager::drawPillButton(int x, int y, int w, int h, const std::string& text, bool focused, const std::string& subtext, bool hasPlayIcon) {
    int r = h / 2;
    uint32_t bgColor = 0xFFFFFFFF;
    uint32_t fgColor = 0xFF0E0B16;
    uint32_t subColor = 0xFF4A455A;

    drawRoundedRect(x, y, w, h, r, bgColor);
    if (focused) {
        drawRoundedBorder(x - 3, y - 3, w + 6, h + 6, r + 3, 3, 0xFF9146FF);
    }

    if (subtext.empty()) {
        int tw = fontRenderer_.measureText(text, 22);
        int totalW = tw + (hasPlayIcon ? 28 : 0);
        int startX = x + (w - totalW) / 2;
        int ty = y + (h - 22) / 2;

        if (hasPlayIcon) {
            drawPlayTriangleIcon(startX + 8, y + h / 2, 10, fgColor);
            drawText(startX + 24, ty, 2, text, fgColor);
        } else {
            drawText(startX, ty, 2, text, fgColor);
        }
    } else {
        int tw1 = fontRenderer_.measureText(text, 18);
        int tx1 = x + (w - tw1) / 2;
        drawText(tx1, y + 10, 1, text, fgColor);
        int tw2 = fontRenderer_.measureText(subtext, 14);
        int tx2 = x + (w - tw2) / 2;
        drawText(tx2, y + 36, 1, subtext, subColor);
    }
}
namespace {
void drawCard(DisplayManager& disp, int x, int y, int w, int h, uint32_t bgColor, uint32_t borderColor, int borderWidth = 1) {
    disp.fillRect(x, y, w, h, bgColor);
    if (borderWidth > 0 && (borderColor & 0xFF000000) != 0) {
        disp.fillRect(x, y, w, borderWidth, borderColor);
        disp.fillRect(x, y + h - borderWidth, w, borderWidth, borderColor);
        disp.fillRect(x, y, borderWidth, h, borderColor);
        disp.fillRect(x + w - borderWidth, y, borderWidth, h, borderColor);
    }
}

// Vector check mark (the UI font has no reliable U+2713 glyph)
void drawCheckGlyph(DisplayManager& d, int x, int y, int s, uint32_t c) {
    for (int i = 0; i <= s / 3; ++i) d.fillRect(x + i, y + s / 2 + i, 3, 3, c);
    for (int i = 0; i <= (s * 2) / 3; ++i) d.fillRect(x + s / 3 + i, y + s / 2 + s / 3 - i, 3, 3, c);
}
} // namespace

void DisplayManager::drawText(int x, int y, int scale, const std::string& text, uint32_t colorArgb) {
    if (!isReady_ || scale <= 0 || text.empty()) return;

    int fontSize = 24;
    switch (scale) {
        case 1: fontSize = 16; break;
        case 2: fontSize = 20; break;
        case 3: fontSize = 26; break;
        case 4: fontSize = 34; break;
        case 5: fontSize = 44; break;
        case 6: fontSize = 54; break;
        case 7: fontSize = 66; break;
        case 8: fontSize = 110; break;
        default: fontSize = scale * 9; break;
    }
    drawTextPx(x, y, fontSize, text, colorArgb);
}

void DisplayManager::drawTextPx(int x, int y, int fontSizePx, const std::string& text, uint32_t colorArgb) {
    if (!isReady_ || fontSizePx <= 0 || text.empty()) return;

    int cursorX = x;
    int cursorY = y;
    int lineHeight = fontSizePx + (fontSizePx / 3);
    int baseline = static_cast<int>(fontSizePx * 0.82f);

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

        if (codepoint == '\n') {
            cursorX = x;
            cursorY += lineHeight;
            continue;
        }

        if (codepoint == 0x2122) { // TM
            const auto& gT = fontRenderer_.getGlyph('T', fontSizePx * 3 / 4);
            const auto& gM = fontRenderer_.getGlyph('M', fontSizePx * 3 / 4);
            if (gT.advance > 0) {
                int gy = cursorY + (baseline / 2);
                for (int r = 0; r < gT.height; ++r) {
                    for (int c = 0; c < gT.width; ++c) {
                        uint8_t a = gT.bitmap[r * gT.width + c];
                        if (a > 0) blendPixel(cursorX + gT.xoff + c, gy + gT.yoff + r, colorArgb, a);
                    }
                }
                cursorX += gT.advance;
                for (int r = 0; r < gM.height; ++r) {
                    for (int c = 0; c < gM.width; ++c) {
                        uint8_t a = gM.bitmap[r * gM.width + c];
                        if (a > 0) blendPixel(cursorX + gM.xoff + c, gy + gM.yoff + r, colorArgb, a);
                    }
                }
                cursorX += gM.advance + 2;
                continue;
            }
        }

        const auto& glyph = fontRenderer_.getGlyph(codepoint, fontSizePx);
        int gx = cursorX + glyph.xoff;
        int gy = cursorY + baseline + glyph.yoff;

        for (int r = 0; r < glyph.height; ++r) {
            for (int c = 0; c < glyph.width; ++c) {
                uint8_t a = glyph.bitmap[r * glyph.width + c];
                if (a > 0) {
                    if (a == 255) {
                        putPixel(gx + c, gy + r, colorArgb);
                    } else {
                        blendPixel(gx + c, gy + r, colorArgb, a);
                    }
                }
            }
        }
        cursorX += glyph.advance;
    }
}

void DisplayManager::drawQrCode(int x, int y, int moduleScale, const std::string& text) {
    if (!isReady_ || !mappedMemory_ || text.empty() || moduleScale <= 0) return;

    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];

    bool ok = qrcodegen_encodeText(text.c_str(), tempBuffer, qrcode,
                                   qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 10,
                                   qrcodegen_Mask_AUTO, true);
    if (!ok) return;

    int size = qrcodegen_getSize(qrcode);
    int quietZone = 2;
    int totalModules = size + quietZone * 2;
    int boxPixels = totalModules * moduleScale;

    fillRect(x, y, boxPixels, boxPixels, 0xFFFFFFFF);

    for (int my = 0; my < size; ++my) {
        for (int mx = 0; mx < size; ++mx) {
            if (qrcodegen_getModule(qrcode, mx, my)) {
                fillRect(x + (mx + quietZone) * moduleScale,
                         y + (my + quietZone) * moduleScale,
                         moduleScale, moduleScale, 0xFF0A0E17);
            }
        }
    }
}

void DisplayManager::drawDashboard(const std::string& consoleIp,
                                   int authPort,
                                   const std::string& serverStatus,
                                   bool padConnected,
                                   uint32_t padButtons,
                                   int16_t stickX,
                                   int16_t stickY,
                                   const std::string& statusText,
                                   uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;

    clear(0xFF090D16);

    fillRect(0, 0, 1920, 96, 0xFF101726);
    fillRect(0, 96, 1920, 2, 0xFF6366F1);

    drawText(70, 28, 5, "Amazon Luna", 0xFFFFFFFF);
    drawText(400, 36, 3, "PANEL DE CONTROL PS5", 0xFFA5B4FC);

    // Tarjeta superior: Vinculación LAN + QR
    drawCard(*this, 70, 136, 1780, 410, 0xFF121927, 0xFF1F293D, 2);

    drawText(110, 175, 4, "1. ESCANEA EL QR O ACCEDE DESDE TU NAVEGADOR", 0xFFFFFFFF);
    drawText(110, 225, 2, "Conecta tu movil o PC a la misma red y abre esta direccion:", 0xFF94A3B8);

    std::string urlText = "http://" + consoleIp + ":" + std::to_string(authPort);
    drawCard(*this, 110, 265, 1150, 70, 0xFF0B101B, 0xFF2563EB, 1);
    fillRect(110, 265, 6, 70, 0xFF38BDF8);
    drawText(130, 283, 4, urlText, 0xFF38BDF8);

    drawText(110, 365, 2, "Servidor HTTP: " + serverStatus, 0xFF38BDF8);
    drawText(110, 410, 2, "Estado Stream: " + statusText, 0xFFFBBF24);

    // QR Code a la derecha
    int qrCanvasSize = 340;
    int qrX = 1450;
    int qrY = 165;
    drawCard(*this, qrX, qrY, qrCanvasSize, qrCanvasSize, 0xFFFFFFFF, 0xFFE2E8F0, 3);
    drawQrCode(qrX + 15, qrY + 15, 7, urlText);

    // Tarjeta inferior: DualSense y Hardware
    drawCard(*this, 70, 580, 1780, 380, 0xFF121927, 0xFF1F293D, 2);

    drawText(110, 620, 4, "2. ESTADO DEL CONTROLADOR DUALSENSE Y HARDWARE", 0xFFFFFFFF);

    std::string padStatus = padConnected ? "CONECTADO (DualSense Activo)" : "NO DETECTADO";
    drawText(110, 680, 3, "Mando PS5: " + padStatus, padConnected ? 0xFF34D399 : 0xFFEF4444);

    char btnHex[128];
    std::snprintf(btnHex, sizeof(btnHex), "Botones Activos: 0x%04X   |   Stick Izquierdo: (%d, %d)",
                  static_cast<unsigned>(padButtons), static_cast<int>(stickX), static_cast<int>(stickY));
    drawText(110, 740, 2, btnHex, 0xFFE2E8F0);

    drawText(110, 800, 2, "Decodificador Video: Hardware PS5 Vdec2 (1080p60)   |   Audio: PCM 48kHz Estereo", 0xFF94A3B8);

    uint32_t pulseColor = ((frameCount / 30) % 2 == 0) ? 0xFF10B981 : 0xFF059669;
    fillRect(110, 860, 18, 18, pulseColor);
    drawText(140, 856, 2, "Motor grafico 60 FPS activo - Pulsa L1+R1+OPTIONS+TOUCHPAD para salir", 0xFFCBD5E1);

    fillRect(0, 1020, 1920, 60, 0xFF090D16);
    drawText(70, 1038, 2, "Amazon Luna PS5 Native Edition v1.4.0", 0xFF475569);

    present();
}

void DisplayManager::drawLoginScreen(const std::string& publicCode,
                                     const std::string& verificationUri,
                                     int qrSize,
                                     const std::function<bool(int, int)>& qrModuleLookup,
                                     const std::string& statusText,
                                     uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;

    // 1. Fondo principal: Obsidian Slate oscuro elegante
    clear(0xFF090D16);

    // 2. Barra de navegacion superior estilo PlayStation 5
    fillRect(0, 0, 1920, 96, 0xFF101726);
    fillRect(0, 96, 1920, 2, 0xFF6366F1);

    drawText(70, 28, 5, "Amazon Luna", 0xFFFFFFFF);

    fillRect(410, 36, 150, 28, 0xFF1E293B);
    drawText(422, 40, 1, "PLAYSTATION 5", 0xFF94A3B8);

    drawText(1480, 34, 3, "INICIAR SESION", 0xFF818CF8);

    // 3. Tarjeta Izquierda: Instrucciones y Codigo de Vinculacion
    int leftCardX = 70;
    int leftCardY = 136;
    int leftCardW = 1080;
    int leftCardH = 780;

    drawCard(*this, leftCardX, leftCardY, leftCardW, leftCardH, 0xFF121927, 0xFF1F293D, 2);

    drawText(leftCardX + 50, leftCardY + 45, 4, "Vincula tu consola con Amazon", 0xFFFFFFFF);
    drawText(leftCardX + 50, leftCardY + 95, 2, "Autoriza este dispositivo en tu cuenta para acceder a tu catalogo de juegos.", 0xFF94A3B8);

    // Paso 1
    fillRect(leftCardX + 50, leftCardY + 145, 80, 26, 0xFF312E81);
    drawText(leftCardX + 62, leftCardY + 148, 1, "PASO 1", 0xFFA5B4FC);
    drawText(leftCardX + 145, leftCardY + 146, 2, "Abre tu navegador web en un telefono movil o PC:", 0xFFCBD5E1);

    std::string fullUrl = "https://" + verificationUri;
    drawCard(*this, leftCardX + 50, leftCardY + 185, 840, 70, 0xFF0B101B, 0xFF2563EB, 1);
    fillRect(leftCardX + 50, leftCardY + 185, 6, 70, 0xFF38BDF8);
    drawText(leftCardX + 80, leftCardY + 203, 4, fullUrl, 0xFF38BDF8);

    // Paso 2
    fillRect(leftCardX + 50, leftCardY + 285, 80, 26, 0xFF78350F);
    drawText(leftCardX + 62, leftCardY + 288, 1, "PASO 2", 0xFFFDE68A);
    drawText(leftCardX + 145, leftCardY + 286, 2, "Introduce este codigo de autorizacion cuando te lo pida:", 0xFFCBD5E1);

    drawCard(*this, leftCardX + 50, leftCardY + 325, 840, 150, 0xFF080C14, 0xFFD97706, 2);

    std::string codeDisplay = publicCode.empty() ? "SOLICITANDO CODIGO..." : publicCode;
    uint32_t codeColor = publicCode.empty() ? 0xFF64748B : 0xFFFBBF24;
    int codeScale = publicCode.empty() ? 5 : 7;
    int codeTextX = publicCode.empty() ? (leftCardX + 160) : (leftCardX + 220);
    drawText(codeTextX, leftCardY + 365, codeScale, codeDisplay, codeColor);

    fillRect(leftCardX + 50, leftCardY + 520, leftCardW - 100, 1, 0xFF1E293B);

    bool hasError = (statusText.find("Error") != std::string::npos || statusText.find("fallo") != std::string::npos);
    uint32_t pulseColor = hasError ? 0xFFEF4444 : (((frameCount / 30) % 2 == 0) ? 0xFF10B981 : 0xFF059669);

    fillRect(leftCardX + 50, leftCardY + 560, 18, 18, pulseColor);
    std::string stateHeading = hasError ? "Reintentando conexion con Amazon..." : "Esperando autorizacion en Amazon...";
    drawText(leftCardX + 80, leftCardY + 556, 3, stateHeading, hasError ? 0xFFFCA5A5 : 0xFF34D399);

    drawText(leftCardX + 50, leftCardY + 605, 2, "Detalle: " + statusText, 0xFF94A3B8);
    drawText(leftCardX + 50, leftCardY + 655, 2, "La pantalla avanzara sola a tu catalogo de juegos una vez aprobado.", 0xFF64748B);

    // 4. Tarjeta Derecha: Codigo QR
    int rightCardX = 1200;
    int rightCardY = 136;
    int rightCardW = 650;
    int rightCardH = 780;

    drawCard(*this, rightCardX, rightCardY, rightCardW, rightCardH, 0xFF121927, 0xFF1F293D, 2);

    drawText(rightCardX + 50, rightCardY + 45, 4, "O Escanea con el Movil", 0xFFFFFFFF);
    drawText(rightCardX + 50, rightCardY + 95, 2, "Apunta con la camara de tu smartphone para abrir amazon.com/code", 0xFF94A3B8);

    int qrCanvasSize = 460;
    int qrCanvasX = rightCardX + (rightCardW - qrCanvasSize) / 2;
    int qrCanvasY = rightCardY + 150;

    drawCard(*this, qrCanvasX, qrCanvasY, qrCanvasSize, qrCanvasSize, 0xFFFFFFFF, 0xFFE2E8F0, 4);

    if (qrSize > 0 && qrModuleLookup) {
        int quietZone = 2;
        int totalModules = qrSize + (quietZone * 2);
        int moduleScale = (qrCanvasSize - 20) / totalModules;
        if (moduleScale < 1) moduleScale = 1;

        int totalQrPx = qrSize * moduleScale;
        int offsetX = qrCanvasX + (qrCanvasSize - totalQrPx) / 2;
        int offsetY = qrCanvasY + (qrCanvasSize - totalQrPx) / 2;

        for (int y = 0; y < qrSize; ++y) {
            for (int x = 0; x < qrSize; ++x) {
                if (qrModuleLookup(x, y)) {
                    fillRect(offsetX + x * moduleScale, offsetY + y * moduleScale,
                             moduleScale, moduleScale, 0xFF0A0E17);
                }
            }
        }
    }

    drawText(rightCardX + 160, rightCardY + 640, 2, "Enlace directo oficial a Amazon CBL", 0xFF64748B);

    // 5. Barra inferior (Footer de la aplicacion)
    fillRect(0, 1020, 1920, 60, 0xFF0B0F19);
    fillRect(0, 1020, 1920, 1, 0xFF1E293B);
    drawText(70, 1038, 2, "DualSense: Pulsa [L1 + R1 + OPTIONS + TOUCHPAD] para cerrar la aplicacion", 0xFF475569);

    present();
}

namespace {

std::string utf8SafeTruncate(const std::string& str, size_t maxBytes) {
    if (str.size() <= maxBytes) return str;
    size_t pos = maxBytes;
    while (pos > 0 && (static_cast<uint8_t>(str[pos]) & 0xC0) == 0x80) {
        pos--;
    }
    return str.substr(0, pos) + "...";
}

std::vector<std::string> utf8WrapLines(const std::string& text, size_t maxCharsPerLine, size_t maxLines) {
    std::vector<std::string> lines;
    std::string current;
    size_t i = 0;

    while (i < text.size() && lines.size() < maxLines) {
        if (text[i] == '\r') {
            i++;
            continue;
        }
        if (text[i] == '\n') {
            if (!current.empty()) {
                lines.push_back(current);
                current.clear();
            }
            i++;
            continue;
        }

        // Find next word
        size_t wordEnd = i;
        while (wordEnd < text.size() && text[wordEnd] != ' ' && text[wordEnd] != '\n' && text[wordEnd] != '\r') {
            wordEnd++;
        }
        std::string word = text.substr(i, wordEnd - i);

        if (current.empty()) {
            current = word;
        } else if (current.size() + 1 + word.size() <= maxCharsPerLine) {
            current += " " + word;
        } else {
            lines.push_back(current);
            if (lines.size() >= maxLines) {
                lines.back() = utf8SafeTruncate(lines.back(), maxCharsPerLine - 3);
                return lines;
            }
            current = word;
        }

        i = wordEnd;
        while (i < text.size() && text[i] == ' ') i++;
    }

    if (!current.empty() && lines.size() < maxLines) {
        lines.push_back(current);
    }
    return lines;
}

} // namespace

void DisplayManager::drawHoverPreviewCard(int anchorX, int anchorY, const GameTile& tile, uint64_t frameCount) {
    (void)frameCount;
    int cardW = 330;
    int cardH = 380;

    int hx = anchorX - 65;
    hx = std::clamp(hx, 40, 1920 - cardW - 40);
    int hy = anchorY - 40;
    if (hy + cardH > 1040) hy = 1040 - cardH;
    if (hy < 60) hy = 60;

    // Sombra exterior oscura suave
    drawRoundedRect(hx - 4, hy - 4, cardW + 8, cardH + 8, 16, 0x90000000);

    // Fondo gris carbón oscuro #1C1A24 (NO morado) con borde sutil #322F40 de 1 px
    drawRoundedRect(hx, hy, cardW, cardH, 12, 0xFF1C1A24);
    drawRoundedBorder(hx, hy, cardW, cardH, 12, 1, 0xFF322F40);

    // 1. Mitad superior (330 x 186 px, y = hy .. hy + 186)
    int vidW = 330;
    int vidH = 186;
    auto& player = luna::media::Mp4PreviewPlayer::instance();
    bool drewVideo = false;

    if (player.hasFrame() && player.currentUrl() == tile.videoUrl) {
        const uint32_t* vPix = player.getFrameRgba();
        if (vPix) {
            drawPreScaledImage(hx, hy, reinterpret_cast<const uint8_t*>(vPix), vidW, vidH, vidW, vidH);
            drewVideo = true;
        }
    }

    if (!drewVideo) {
        std::string prevUrl = tile.backdropUrl.empty() ? tile.coverUrl : tile.backdropUrl;
        auto prevImg = luna::util::ImageLoader::instance().getImage(prevUrl, luna::util::ImagePreset::HorizontalCard16_9);
        if (prevImg && prevImg->ready) {
            drawPreScaledImage(hx, hy, prevImg->rgba.data(), vidW, vidH, prevImg->width, prevImg->height);
        } else {
            if (!prevUrl.empty()) luna::util::ImageLoader::instance().requestImage(prevUrl, luna::util::ImagePreset::HorizontalCard16_9);
            drawRoundedRect(hx, hy, vidW, vidH, 12, 0xFF241B3B);
        }
    }

    // Icono pequeño de clasificación de contenido del vídeo en esquina superior izquierda
    std::string smallPegi = "12";
    if (tile.pegiRating.find("18") != std::string::npos) smallPegi = "18";
    else if (tile.pegiRating.find("16") != std::string::npos) smallPegi = "16";
    else if (tile.pegiRating.find("7") != std::string::npos) smallPegi = "7";
    else if (tile.pegiRating.find("3") != std::string::npos) smallPegi = "3";
    uint32_t smallPegiBg = (smallPegi == "3" || smallPegi == "7") ? 0xFF65B32E : ((smallPegi == "18") ? 0xFFE30613 : 0xFFF58220);
    drawRoundedRect(hx + 10, hy + 10, 36, 22, 4, smallPegiBg);
    drawText(hx + 14, hy + 13, 1, smallPegi, 0xFFFFFFFF);

    // Icono vectorial blanco de altavoz muteado en esquina inferior derecha del vídeo
    drawMutedSpeakerIcon(hx + 304, hy + 162, 16, 0xFFFFFFFF);

    // 2. Botón único de ancho completo (x + 16, y + 198, w = 298, h = 42, r = 8)
    int btnX = hx + 16;
    int btnY = hy + 198;
    int btnW = 298;
    int btnH = 42;
    drawRoundedRect(btnX, btnY, btnW, btnH, 8, 0xFFFFFFFF);
    drawPlayTriangleIcon(btnX + 90, btnY + 21, 12, 0xFF0E0B16);
    drawText(btnX + 110, btnY + 11, 2, "Jugar ahora", 0xFF0E0B16);

    // 3. Bloque inferior de metadatos (y + 252 .. y + 368)
    // Línea 1 (x + 16, y + 252): Título completo en blanco negrita (16 px)
    drawText(hx + 16, hy + 252, 2, utf8SafeTruncate(tile.title, 22), 0xFFFFFFFF);

    // Línea 2 (x + 16, y + 278): Métodos de entrada en gris claro #BEBACC (13 px)
    std::string inputs = tile.inputMethods.empty() ? "Mando, Teclado y ratón" : tile.inputMethods;
    drawText(hx + 16, hy + 278, 1, utf8SafeTruncate(inputs, 28), 0xFFBEBACC);

    // Línea 3 (x + 16, y + 300): Distribuidora / Publisher real en gris claro #BEBACC (13 px)
    std::string pub = tile.publisher.empty() ? "Amazon Luna" : tile.publisher;
    drawText(hx + 16, hy + 300, 1, utf8SafeTruncate(pub, 28), 0xFFBEBACC);

    // Sello PEGI oficial en la parte inferior derecha (x + 266, y + 276, w = 46, h = 56)
    drawPegiBadge(hx + 266, hy + 276, 46, 56, tile.pegiRating);

    // Fila de etiquetas de género abajo a la izquierda (x + 16, y + 332)
    int pillX = hx + 16;
    int pillY = hy + 332;
    std::vector<std::string> genresList;
    std::string curG;
    for (char ch : tile.genres) {
        if (ch == ',' || ch == '/' || ch == ';' || ch == '\xc2' || ch == '\xb7') {
            if (!curG.empty()) { genresList.push_back(curG); curG.clear(); }
        } else {
            curG += ch;
        }
    }
    if (!curG.empty()) genresList.push_back(curG);
    if (genresList.empty()) {
        genresList.push_back("Acción");
        genresList.push_back("Aventura");
    }

    for (size_t g = 0; g < genresList.size() && g < 3; ++g) {
        std::string gn = genresList[g];
        while (!gn.empty() && (gn.front() == ' ' || gn.front() == '\t')) gn.erase(gn.begin());
        while (!gn.empty() && (gn.back() == ' ' || gn.back() == '\t')) gn.pop_back();
        if (gn.empty()) continue;

        int tw = fontRenderer_.measureText(gn, 13);
        int pw = tw + 18;
        if (pillX + pw > hx + 260) break;
        drawRoundedRect(pillX, pillY, pw, 24, 6, 0xFF353242);
        drawText(pillX + 9, pillY + 5, 1, gn, 0xFFFFFFFF);
        pillX += pw + 8;
    }
}

void DisplayManager::drawLunaHomeScreen(const std::vector<LunaCollection>& collections,
                                        const std::vector<std::string>& navTabs,
                                        int activeTabIdx,
                                        int focusZone,
                                        int navColIdx,
                                        int selectedRowIdx,
                                        const std::vector<int>& rowSelectedCol,
                                        int heroBtnIdx,
                                        const PlayerProfileInfo& profile,
                                        bool padConnected,
                                        const std::string& statusText,
                                        uint64_t frameCount,
                                        bool profileMenuOpen,
                                        int profileMenuIdx,
                                        bool searchOpen,
                                        int searchZone,
                                        int searchLetterIdx,
                                        int searchGridIdx,
                                        const std::vector<GameTile>& filteredGames,
                                        bool hoverCardOpen) {
    if (!isReady_) return;
    HomeScreenProps props{
        .collections = collections,
        .navTabs = navTabs,
        .activeTabIdx = activeTabIdx,
        .focusZone = focusZone,
        .navColIdx = navColIdx,
        .selectedRowIdx = selectedRowIdx,
        .rowSelectedCol = rowSelectedCol,
        .heroBtnIdx = heroBtnIdx,
        .profile = profile,
        .padConnected = padConnected,
        .statusText = statusText,
        .frameCount = frameCount,
        .profileMenuOpen = profileMenuOpen,
        .profileMenuIdx = profileMenuIdx,
        .searchOpen = searchOpen,
        .searchZone = searchZone,
        .searchLetterIdx = searchLetterIdx,
        .searchGridIdx = searchGridIdx,
        .filteredGames = filteredGames,
        .hoverCardOpen = hoverCardOpen,
        .searchQuery = searchQuery_
    };
    LunaClayUI::renderHomeScreen(*this, props);
}

void DisplayManager::drawGameDetailScreen(const GameTile& game,
                                          const PlayerProfileInfo& profile,
                                          const std::string& statusText,
                                          bool isConnecting,
                                          int detailZone,
                                          int detailBtnIdx,
                                          int similarSelectedIdx,
                                          uint64_t frameCount,
                                          bool isFavorite) {
    if (!isReady_ || !mappedMemory_) return;
    GameDetailProps props{
        .game = game,
        .profile = profile,
        .statusText = statusText,
        .isConnecting = isConnecting,
        .detailZone = detailZone,
        .detailBtnIdx = detailBtnIdx,
        .similarSelectedIdx = similarSelectedIdx,
        .frameCount = frameCount,
        .isFavorite = isFavorite
    };
    LunaClayUI::renderGameDetail(*this, props);
}

void DisplayManager::drawNativeEngineWaitingScreen(const std::string& localWebUrl,
                                                   const std::string& lanWebUrl,
                                                   const std::string& statusMsg,
                                                   uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;

    // 1. Fondo cósmico oficial
    clear(0xFF0E0B16);

    // 2. Barra superior
    fillRect(0, 0, 1920, 72, 0xFF0D0A1C);
    fillRect(0, 72, 1920, 2, 0xFF6D28D9);

    drawLunaLogoIcon(60, 22, 28);
    drawText(100, 18, 5, "luna", 0xFFFFFFFF);
    drawText(220, 26, 3, "MOTOR DE STREAMING NATIVO PS5", 0xFFA5B4FC);

    uint32_t greenGlow = ((frameCount / 30) % 2 == 0) ? 0xFF10B981 : 0xFF059669;
    drawRoundedRect(1560, 20, 300, 34, 17, 0x2210B981);
    drawRoundedBorder(1560, 20, 300, 34, 17, 1, greenGlow);
    fillRect(1580, 32, 10, 10, greenGlow);
    drawText(1600, 27, 2, "1080p 60 FPS LISTO", 0xFFFFFFFF);

    // 3. Panel central
    int cardX = 310;
    int cardY = 150;
    int cardW = 1300;
    int cardH = 740;
    drawCard(*this, cardX, cardY, cardW, cardH, 0xFF161026, 0xFF2C2046, 2);

    // Encabezado
    drawRoundedRect(cardX + 40, cardY + 36, cardW - 80, 96, 12, 0xFF1C152E);
    drawText(cardX + 70, cardY + 54, 4, "Web Oficial de Amazon Luna Activa", 0xFFFFFFFF);
    drawText(cardX + 70, cardY + 98, 2, "Navega por todo el catalogo, portadas, buscador y fichas en el Navegador de tu PS5.", 0xFF94A3B8);

    // Mensaje de estado dinámico
    if (!statusMsg.empty()) {
        drawRoundedRect(cardX + 40, cardY + 146, cardW - 80, 48, 8, 0xFF231B38);
        drawText(cardX + 60, cardY + 161, 2, statusMsg, 0xFF38BDF8);
    }

    // Caja 1: Consola PS5
    int boxY1 = cardY + 210;
    drawRoundedRect(cardX + 40, boxY1, cardW - 80, 96, 12, 0xFF1F1833);
    drawRoundedBorder(cardX + 40, boxY1, cardW - 80, 96, 12, 1, 0xFF4C1D95);
    drawText(cardX + 70, boxY1 + 18, 2, "EN ESTA PLAYSTATION 5 (NAVEGADOR WEB)", 0xFFA78BFA);
    drawText(cardX + 70, boxY1 + 50, 4, localWebUrl, 0xFFFFFFFF);

    // Caja 2: Red Local
    int boxY2 = cardY + 322;
    drawRoundedRect(cardX + 40, boxY2, cardW - 80, 96, 12, 0xFF1F1833);
    drawRoundedBorder(cardX + 40, boxY2, cardW - 80, 96, 12, 1, 0xFF4C1D95);
    drawText(cardX + 70, boxY2 + 18, 2, "DESDE CUALQUIER PC / TABLET / MOVIL EN TU RED", 0xFFA78BFA);
    drawText(cardX + 70, boxY2 + 50, 4, lanWebUrl, 0xFF38BDF8);

    // Instrucción de juego
    int boxY3 = cardY + 434;
    drawRoundedRect(cardX + 40, boxY3, cardW - 80, 100, 12, 0xFF120E22);
    drawText(cardX + 70, boxY3 + 24, 3, "Al pulsar 'Jugar ahora' en la Web:", 0xFFFCD34D);
    drawText(cardX + 70, boxY3 + 60, 2, "El motor WebRTC + decodificador H.264 (libSceVideodec2) iniciara aqui a pantalla completa y 60 FPS.", 0xFFE2E8F0);

    // Botones de acción
    int btnY = cardY + 560;
    uint32_t btnXBorder = ((frameCount / 20) % 2 == 0) ? 0xFF02FDDB : 0xFF9146FF;
    drawRoundedRect(cardX + 60, btnY, 440, 60, 30, 0xFF281C44);
    drawRoundedBorder(cardX + 60, btnY, 440, 60, 30, 2, btnXBorder);
    drawText(cardX + 85, btnY + 18, 3, "[X] Abrir Web en PS5", 0xFFFFFFFF);

    drawRoundedRect(cardX + 530, btnY, 360, 60, 30, 0xFF1F1833);
    drawRoundedBorder(cardX + 530, btnY, 360, 60, 30, 1, 0xFF4C1D95);
    drawText(cardX + 555, btnY + 18, 3, "[CUADRADO] Desvincular QR", 0xFFCBD5E1);

    drawRoundedRect(cardX + 920, btnY, 300, 60, 30, 0xFF1F1833);
    drawRoundedBorder(cardX + 920, btnY, 300, 60, 30, 1, 0xFF4C1D95);
    drawText(cardX + 945, btnY + 18, 3, "[OPTIONS] Salir", 0xFF94A3B8);

    present();
}

void DisplayManager::drawStreamingStatusScreen(const GameTile& game,
                                               const std::string& statusText,
                                               const std::string& statsText,
                                               const std::string& iceStatsText,
                                               uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;

    // 1. Fondo principal ultra-oscuro estilo Cloud Gaming
    clear(0xFF070510);

    // 2. Barra superior
    fillRect(0, 0, 1920, 72, 0xFF0D0A1C);
    fillRect(0, 72, 1920, 2, 0xFF6D28D9);

    fillRect(60, 28, 14, 14, 0xFF8B5CF6);
    drawText(85, 18, 5, "luna", 0xFFFFFFFF);
    drawText(220, 25, 3, "TRANSMISION EN DIRECTO", 0xFFA5B4FC);

    // Indicador pulsante de transmision
    uint32_t liveDot = ((frameCount / 25) % 2 == 0) ? 0xFFEF4444 : 0xFF7F1D1D;
    fillRect(1700, 28, 12, 12, liveDot);
    drawText(1722, 24, 2, "CLOUD WEBRTC", 0xFFE2E8F0);

    // 3. Tarjeta central de streaming
    int cardX = 260;
    int cardY = 160;
    int cardW = 1400;
    int cardH = 720;
    drawCard(*this, cardX, cardY, cardW, cardH, 0xFF120E24, 0xFF2A1C52, 2);

    // Portada del juego a la izquierda si existe
    int imgX = cardX + 60;
    int imgY = cardY + 60;
    int imgW = 280;
    int imgH = 400;
    if (!game.coverRgba.empty() && game.coverWidth > 0 && game.coverHeight > 0) {
        drawImage(imgX, imgY, imgW, imgH, game.coverRgba.data(), game.coverWidth, game.coverHeight);
        drawCard(*this, imgX, imgY, imgW, imgH, 0x00000000, 0xFF6D28D9, 2);
    } else {
        drawCard(*this, imgX, imgY, imgW, imgH, 0xFF1E1538, 0xFF4C1D95, 2);
        fillRect(imgX + 20, imgY + 20, imgW - 40, imgH - 40, game.badgeColor);
        drawText(imgX + 30, imgY + 180, 4, game.category, 0xFFFFFFFF);
    }

    // Informacion del juego a la derecha
    int textX = imgX + imgW + 60;
    int textY = imgY + 20;

    // Badge de categoria
    fillRect(textX, textY, 140, 32, 0xFF2E1065);
    drawText(textX + 16, textY + 6, 2, game.category, 0xFFA78BFA);

    // Titulo del juego grande
    drawText(textX, textY + 50, 6, game.title, 0xFFFFFFFF);
    drawText(textX, textY + 120, 3, "Distribuidora: " + game.publisher, 0xFF94A3B8);

    // Separador decorativo
    fillRect(textX, textY + 165, 800, 2, 0xFF2A1C52);

    // Spinner o barra de progreso animada
    int barW = 800;
    int barH = 10;
    int barX = textX;
    int barY = textY + 195;
    fillRect(barX, barY, barW, barH, 0xFF1E1538);
    int pulsePos = static_cast<int>((frameCount * 8) % (barW + 200)) - 100;
    int segW = 140;
    int segStart = std::max(barX, barX + pulsePos);
    int segEnd = std::min(barX + barW, barX + pulsePos + segW);
    if (segEnd > segStart) {
        fillRect(segStart, barY, segEnd - segStart, barH, 0xFF8B5CF6);
    }

    // Estado principal
    drawText(textX, textY + 225, 4, statusText, 0xFFFDE68A);

    // Metricas tecnicas WebRTC / Hardware
    fillRect(textX, textY + 275, 800, 95, 0xFF17112C);
    drawText(textX + 20, textY + 287, 2, statsText, 0xFF38BDF8);
    drawText(textX + 20, textY + 315, 2, iceStatsText, 0xFFFCD34D);
    drawText(textX + 20, textY + 343, 2, "Hardware: libSceVideodec2 (H.264 High 1080p60) + SceAudioOut (Opus 48kHz)", 0xFF34D399);

    // 4. Barra inferior de control
    fillRect(0, 980, 1920, 100, 0xFF0D0A1C);
    fillRect(0, 980, 1920, 2, 0xFF241842);
    drawText(70, 1018, 2, "[CIRCULO (O)] Cancelar y volver al catalogo   |   [L1 + R1 + OPTIONS] o [OPTIONS + TOUCH] Salir del stream", 0xFFE2E8F0);

    present();
}

bool DisplayManager::presentYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                  int strideY, int strideU, int strideV,
                                  int width, int height) {
    return presentYuv420WithHud(y, u, v, strideY, strideU, strideV, width, height, false, "", "");
}

bool DisplayManager::presentYuv420WithHud(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                          int strideY, int strideU, int strideV,
                                          int width, int height,
                                          bool showHud,
                                          const std::string& hudLine1,
                                          const std::string& hudLine2,
                                          bool overlayOpen,
                                          int overlaySelectedIdx,
                                          bool hudPinned,
                                          const std::string& gameTitle) {
    if (!isReady_ || !y || !u || !v) return false;

    for (int j = 0; j < height_ && j < height; ++j) {
        const uint8_t* yRow = y + (j * strideY);
        const uint8_t* uRow = u + ((j / 2) * strideU);
        const uint8_t* vRow = v + ((j / 2) * strideV);
        uint32_t* outRow = &canvas_[j * width_];

        for (int i = 0; i < width_ && i < width; ++i) {
            int yVal = yRow[i];
            int uVal = uRow[i / 2] - 128;
            int vVal = vRow[i / 2] - 128;

            int c = (yVal - 16) * 1192;
            int r = clamp8((c + 1836 * vVal + 512) >> 10);
            int g = clamp8((c - 218 * uVal - 546 * vVal + 512) >> 10);
            int b = clamp8((c + 2163 * uVal + 512) >> 10);

            outRow[i] = 0xFF000000U | (b << 16) | (g << 8) | r;
        }
    }

    if (showHud && !overlayOpen && !hudLine1.empty()) {
        int hudH = hudLine2.empty() ? 38 : 66;
        fillRect(20, 16, 1880, hudH, 0xFF130E21);
        fillRect(20, 16, 1880, 2, 0xFF9146FF);
        drawText(34, 23, 2, hudLine1, 0xFF02FDDB);
        if (!hudLine2.empty()) {
            drawText(34, 51, 1, hudLine2, 0xFFC8A2FF);
        }
    }

    if (overlayOpen) {
        int ovW = 920;
        int ovH = 540;
        int ovX = (1920 - ovW) / 2;
        int ovY = (1080 - ovH) / 2;

        drawCard(*this, ovX, ovY, ovW, ovH, 0xFF130E21, 0xFF9146FF, 3);
        fillRect(ovX + 3, ovY + 3, ovW - 6, 72, 0xFF1F1638);
        fillRect(ovX + 3, ovY + 75, ovW - 6, 2, 0xFF382B54);

        drawLunaLogoIcon(ovX + 36, ovY + 26, 22);
        drawText(ovX + 70, ovY + 20, 5, "luna", 0xFFFFFFFF);
        drawText(ovX + 195, ovY + 29, 2, "MENU DE PARTIDA EN LA NUBE", 0xFF02FDDB);

        if (!gameTitle.empty()) {
            drawText(ovX + 36, ovY + 94, 3, utf8SafeTruncate("Jugando a: " + gameTitle, 48), 0xFFFFFFFF);
        }

        // Panel de telemetria en tiempo real
        drawCard(*this, ovX + 36, ovY + 140, ovW - 72, 76, 0xFF1C162B, 0xFF2D224C, 1);
        drawText(ovX + 54, ovY + 152, 2, "Estado del Stream: 1080p 60 FPS Full HD  •  WebRTC Conectado", 0xFF02FDDB);
        drawText(ovX + 54, ovY + 182, 1, utf8SafeTruncate(hudLine1, 92), 0xFFC8A2FF);

        // 3 Botones interactivos del Overlay
        const std::string btnLabels[3] = {
            "Reanudar partida",
            std::string("HUD de rendimiento en pantalla: ") + (hudPinned ? "[FIJADO VISIBLE]" : "[OCULTO AUTOMÁTICO]"),
            "Salir del juego y volver al catálogo de Amazon Luna"
        };

        int btnY = ovY + 240;
        for (int b = 0; b < 3; ++b) {
            bool isFocused = (b == overlaySelectedIdx);
            uint32_t bg = isFocused ? ((b == 2) ? 0xFFB91C1C : 0xFF9146FF) : 0xFF261E3A;
            uint32_t border = isFocused ? 0xFFFFFFFF : 0xFF382B54;
            drawCard(*this, ovX + 36, btnY, ovW - 72, 62, bg, border, isFocused ? 3 : 1);
            drawText(ovX + 64, btnY + 18, 3, btnLabels[b], 0xFFFFFFFF);
            btnY += 76;
        }

        drawText(ovX + 36, ovY + ovH - 42, 1,
                 "[ARRIBA / ABAJO] Seleccionar opción   |   [(X)] Confirmar   |   [(O)] Cerrar menú y seguir jugando",
                 0xFFB8B2C8);
    }

    present();
    return true;
}


void DisplayManager::drawSettingsScreen(int selectedItemIdx,
                                        const std::string& statusText,
                                        uint64_t frameCount,
                                        int accountsSubTab) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    clear(0xFF0E0B16);

    // Barra superior
    fillRect(0, 0, 1920, 90, 0xFF181226);
    fillRect(0, 90, 1920, 2, 0xFF9146FF);
    drawLunaLogoIcon(50, 24, 42);
    drawText(105, 30, 4, "Ajustes y Configuracion de Amazon Luna", 0xFFFFFFFF);
    drawText(1580, 36, 2, "[(O) Volver al inicio]", 0xFFCBD5E1);

    // 10 Secciones oficiales de Ajustes
    struct SettingSection {
        const char* title;
        const char* desc;
        const char* icon;
    };
    static const SettingSection kSections[10] = {
        {"Mandos", "DualSense PS5, Mando Luna, Bluetooth", "[M]"},
        {"Cuentas vinculadas", "Ubisoft, EA, Epic Games, GOG, Twitch", "[C]"},
        {"Mi cuenta", "Jesus (Bornicho #8663) - Luna Estandar", "[U]"},
        {"Control parental", "Bloqueo por edades PEGI y PIN de 5 digitos", "[P]"},
        {"Calidad de vídeo", "1080p 60 FPS (Recomendado) o 720p", "[Q]"},
        {"Idioma", "13 idiomas disponibles (Espanol, English...)", "[L]"},
        {"Datos respaldados de juego", "Sincronizacion en nube Amazon Cloud Save", "[S]"},
        {"Descargar Luna", "Disponible en PC, Mac, Fire TV, iOS y Android", "[D]"},
        {"Ayuda y comentarios", "Diagnostico de conexion y soporte tecnico", "[H]"},
        {"Acerca de y aspectos jurídicos", "Version de cliente v1.8.197368.0", "[A]"}
    };

    int startY = 115;
    int optW = 750;
    int optH = 78;
    int optGap = 12;

    for (int i = 0; i < 10; ++i) {
        int y = startY + i * (optH + optGap);
        if (y + optH > 1050) break;
        bool focused = (selectedItemIdx == i);

        uint32_t bg = focused ? 0xFF9146FF : 0xFF1C152E;
        uint32_t border = focused ? 0xFFFFFFFF : 0xFF2D2248;
        drawRoundedRect(50, y, optW, optH, 12, bg);
        drawRoundedBorder(50, y, optW, optH, 12, focused ? 3 : 1, border);

        drawRoundedRect(68, y + 18, 42, 42, 8, focused ? 0xFF7C3AED : 0xFF281E42);
        drawText(78, y + 28, 2, kSections[i].icon, focused ? 0xFFFFFFFF : 0xFFA78BFA);

        drawText(124, y + 16, 2, kSections[i].title, 0xFFFFFFFF);
        drawText(124, y + 44, 1, kSections[i].desc, focused ? 0xFFFCD34D : 0xFF94A3B8);

        if (focused) {
            drawText(optW - 10, y + 28, 2, "▶", 0xFFFFFFFF);
        }
    }

    // Panel de detalle a la derecha (x = 830, w = 1040, h = 900)
    int pX = 830;
    int pW = 1040;
    int pH = 910;
    drawRoundedRect(pX, startY, pW, pH, 16, 0xFF161024);
    drawRoundedBorder(pX, startY, pW, pH, 16, 1, 0xFF2D2248);

    drawText(pX + 40, startY + 32, 4, kSections[selectedItemIdx].title, 0xFFFFFFFF);
    fillRect(pX + 40, startY + 74, pW - 80, 1, 0xFF9146FF);

    int dY = startY + 95;
    if (selectedItemIdx == 0) {
        // Mandos y Gamepads
        drawText(pX + 40, dY, 3, "Mandos actualmente conectados:", 0xFFFFFFFF);
        drawRoundedRect(pX + 40, dY + 40, pW - 80, 100, 12, 0xFF22173B);
        drawText(pX + 65, dY + 60, 3, "Mando inalambrico DualSense PS5", 0xFFFFFFFF);
        drawText(pX + 65, dY + 95, 2, "Bateria: 100% • Respuesta nativa: 0 ms • Vibracion activa", 0xFF10B981);

        drawText(pX + 40, dY + 180, 3, "Mando oficial Amazon Luna:", 0xFFFFFFFF);
        drawText(pX + 40, dY + 220, 2, "Puedes conectar un mando Amazon Luna via Cloud Direct o Bluetooth.", 0xFFCBD5E1);
        drawPillButton(pX + 40, dY + 265, 380, 60, "Instrucciones de emparejamiento", true, "", false);
    } else if (selectedItemIdx == 1) {
        // Cuentas vinculadas con 2 subpestañas
        drawPillButton(pX + 40, dY - 10, 380, 50, "Cuentas vinculadas para jugar", accountsSubTab == 0, "", false);
        drawPillButton(pX + 440, dY - 10, 420, 50, "Cuentas vinculadas para reclamar juegos", accountsSubTab == 1, "", false);
        struct LinkedAccount {
            const char* provider;
            const char* status;
        };
        static const LinkedAccount kPlayAccounts[] = {
            {"EA", "✓ Vinculada (bornicho1982)"},
            {"EPIC GAMES", "✓ Vinculada (bornicho1982)"},
            {"UBISOFT", "✓ Vinculada (bornicho1982)"},
            {"TWITCH", "✓ Vinculada (bornicho)"},
            {"DISCORD", "✓ Vinculada (bornicho)"}
        };
        static const LinkedAccount kClaimAccounts[] = {
            {"EA", "✓ Vinculada (bornicho1982)"},
            {"EPIC GAMES", "✓ Vinculada (bornicho1982)"},
            {"UBISOFT", "✓ Vinculada (bornicho1982)"},
            {"GOG", "✓ Vinculada (bornicho1982)"},
            {"PRIME GAMING", "✓ Vinculada (bornicho1982)"}
        };
        const LinkedAccount* list = (accountsSubTab == 0) ? kPlayAccounts : kClaimAccounts;
        for (int a = 0; a < 5; ++a) {
            int ay = dY + 65 + a * 70;
            drawRoundedRect(pX + 40, ay, pW - 80, 58, 10, 0xFF1E1633);
            drawText(pX + 64, ay + 17, 2, list[a].provider, 0xFFFFFFFF);
            drawText(pX + pW - 380, ay + 17, 2, list[a].status, 0xFF10B981);
        }
        drawText(pX + 40, dY + 430, 2, "[(X) Gestionar o desvincular cuentas en amazon.es/luna]", 0xFF94A3B8);
    } else if (selectedItemIdx == 2) {
        // Mi cuenta
        drawText(pX + 40, dY, 3, "Jesus (Bornicho #8663)", 0xFFFFFFFF);
        drawText(pX + 40, dY + 40, 2, "Plan actual: Luna Estándar (incluido con Prime)", 0xFF10B981);
        drawText(pX + 40, dY + 80, 2, "Gestiona tu cuenta de Amazon desde amazon.es/luna", 0xFFCBD5E1);
        drawPillButton(pX + 40, dY + 150, 380, 60, "Cerrar sesión de Amazon", true, "", false);
    } else if (selectedItemIdx == 3) {
        // Control parental
        drawText(pX + 40, dY, 3, "Ajustes de restriccion por edad (PEGI):", 0xFFFFFFFF);
        drawText(pX + 40, dY + 40, 2, "Todos los juegos que superen el limite fijado requeriran el PIN.", 0xFFCBD5E1);
        drawRoundedRect(pX + 40, dY + 80, pW - 80, 70, 10, 0xFF22173B);
        drawText(pX + 60, dY + 102, 3, "Nivel de bloqueo actual: DESACTIVADO (Acceso libre)", 0xFF10B981);
        drawPillButton(pX + 40, dY + 175, 300, 58, "Establecer PIN de 5 digitos", true, "", false);
    } else if (selectedItemIdx == 4) {
        // Calidad de audio y video
        drawText(pX + 40, dY, 3, "Resolucion y tasa de fotogramas:", 0xFFFFFFFF);
        drawRoundedRect(pX + 40, dY + 40, pW - 80, 80, 10, 0xFF22173B);
        drawText(pX + 60, dY + 58, 3, "1080p a 60 FPS (Full HD recomendada)", 0xFFFFFFFF);
        drawText(pX + 60, dY + 85, 1, "Consumo aproximado: 10 GB/hora • Maxima nitidez", 0xFF94A3B8);

        drawRoundedRect(pX + 40, dY + 135, pW - 80, 80, 10, 0xFF1C152E);
        drawText(pX + 60, dY + 153, 3, "720p a 60 FPS (Bajo consumo)", 0xFFCBD5E1);
        drawText(pX + 60, dY + 180, 1, "Consumo aproximado: 5 GB/hora • Menor uso de datos", 0xFF94A3B8);
    } else if (selectedItemIdx == 5) {
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
    } else if (selectedItemIdx == 7) {
        // Descargar Luna / Novedades de Luna
        drawText(pX + 40, dY, 3, "Novedades de Amazon Luna (Octubre 2026)", 0xFFFFFFFF);
        drawRoundedRect(pX + 40, dY + 45, pW - 80, 120, 12, 0xFF22173B);
        drawText(pX + 64, dY + 65, 3, "• Sonic X Shadow Generations disponible en Luna+", 0xFF10B981);
        drawText(pX + 64, dY + 100, 2, "• Compatibilidad nativa 1080p 60 FPS con mando DualSense", 0xFFE2E8F0);
        drawText(pX + 64, dY + 125, 2, "• Nuevas salas multijugador GameNight y Remote Play", 0xFFCBD5E1);

        drawText(pX + 40, dY + 190, 3, "Dispositivos compatibles:", 0xFFFFFFFF);
        drawText(pX + 40, dY + 230, 2, "Amazon Luna esta disponible en Fire TV, PC, Mac, iOS, Android y PS5.", 0xFFCBD5E1);
    } else if (selectedItemIdx == 8) {
        // Ayuda y comentarios
        drawText(pX + 40, dY, 3, "Ayuda y Soporte Tecnico", 0xFFFFFFFF);
        drawRoundedRect(pX + 40, dY + 45, pW - 80, 140, 12, 0xFF22173B);
        drawText(pX + 64, dY + 65, 3, "Diagnostico de Conexion:", 0xFFFFFFFF);
        drawText(pX + 64, dY + 98, 2, "• Estado de conexion Cloud: Excelente (< 15 ms)", 0xFF10B981);
        drawText(pX + 64, dY + 124, 2, "• Servidor Tempo Proxy: eu-west-1 (Madrid / Dublin)", 0xFFCBD5E1);
        drawText(pX + 64, dY + 150, 2, "• Decodificador Hardware PS5: libSceVideodec2 Activo", 0xFF10B981);

        drawText(pX + 40, dY + 215, 3, "Soporte oficial de Amazon:", 0xFFFFFFFF);
        drawText(pX + 40, dY + 250, 2, "Para asistencia tecnica visita: amazon.es/ayuda-luna", 0xFF38BDF8);
    } else if (selectedItemIdx == 9) {
        // Acerca de y aspectos juridicos
        drawText(pX + 40, dY, 3, "Acerca de Amazon Luna para PS5", 0xFFFFFFFF);
        drawRoundedRect(pX + 40, dY + 45, pW - 80, 160, 12, 0xFF22173B);
        drawText(pX + 64, dY + 65, 3, "Amazon Luna Client (PS5 Native Edition)", 0xFFFFFFFF);
        drawText(pX + 64, dY + 98, 2, "Version: v1.8.197368.0-ps5 (Engine: Clay UI Flexbox)", 0xFFCBD5E1);
        drawText(pX + 64, dY + 124, 2, "Licencia: Software oficial Amazon Digital Services LLC", 0xFF94A3B8);
        drawText(pX + 64, dY + 150, 2, "Condiciones de uso y privacidad en amazon.es/legal", 0xFF94A3B8);
    } else {
        // Seccion general
        drawText(pX + 40, dY, 2, kSections[selectedItemIdx].desc, 0xFFCBD5E1);
        drawText(pX + 40, dY + 40, 2, "Amazon Luna Client v1.8.197368.0", 0xFF94A3B8);
        drawText(pX + 40, dY + 70, 2, "Servidores europeos Tempo Proxy activos en eu-west-1", 0xFF10B981);
    }

    if (!statusText.empty()) {
        fillRect(0, 1046, 1920, 34, 0xEE0B0912);
        drawText(44, 1054, 1, statusText, 0xFF94A3B8);
    }

    present();
}

void DisplayManager::drawProfileScreen(int kind,
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

void DisplayManager::drawMediaViewerScreen(const GameTile& game,
                                          int currentMediaIdx,
                                          uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    clear(0xFF08060D);

    size_t numScreens = game.screenshotUrls.size();
    if (numScreens == 0) {
        drawText(68, 500, 3, "No hay capturas disponibles para este juego.", 0xFFFFFFFF);
        present();
        return;
    }

    if (currentMediaIdx < 0) currentMediaIdx = 0;
    if (currentMediaIdx >= static_cast<int>(numScreens)) currentMediaIdx = static_cast<int>(numScreens) - 1;

    std::string currentUrl = game.screenshotUrls[currentMediaIdx];

    // Carga robusta: Si no esta en 1080p aun, muestra la miniatura escalada al instante sin bloquear la pantalla
    auto img = luna::util::ImageLoader::instance().getImage(currentUrl, luna::util::ImagePreset::PanoramicDetail);
    if (!img || !img->ready) {
        img = luna::util::ImageLoader::instance().getImage(currentUrl, luna::util::ImagePreset::HorizontalCard16_9);
    }

    if (img && img->ready) {
        drawPreScaledImage(0, 0, img->rgba.data(), 1920, 1080, img->width, img->height);
    } else {
        luna::util::ImageLoader::instance().requestImage(currentUrl, luna::util::ImagePreset::PanoramicDetail);
        luna::util::ImageLoader::instance().requestImage(currentUrl, luna::util::ImagePreset::HorizontalCard16_9);
        fillRect(0, 0, 1920, 1080, 0xFF140F22);
    }

    // Pre-cargar imagenes adyacentes para navegacion instantanea
    if (currentMediaIdx + 1 < static_cast<int>(numScreens)) {
        luna::util::ImageLoader::instance().requestImage(game.screenshotUrls[currentMediaIdx + 1], luna::util::ImagePreset::PanoramicDetail);
    }
    if (currentMediaIdx > 0) {
        luna::util::ImageLoader::instance().requestImage(game.screenshotUrls[currentMediaIdx - 1], luna::util::ImagePreset::PanoramicDetail);
    }

    // Degradados superior e inferior para legibilidad
    fillRect(0, 0, 1920, 90, 0xCC0E0B16);
    fillRect(0, 990, 1920, 90, 0xCC0E0B16);

    drawText(68, 30, 4, game.title, 0xFFFFFFFF);
    std::string countStr = (currentMediaIdx == 0 && !game.videoUrl.empty())
        ? "Trailer oficial"
        : ("Captura " + std::to_string(currentMediaIdx + 1) + " de " + std::to_string(numScreens));
    drawText(1600, 32, 3, countStr, 0xFFFCD34D);

    std::string ctrlHint = "[◀ / L1] Anterior      [▶ / R1] Siguiente      [(O)] Volver a la ficha del juego";
    drawText(68, 1025, 2, ctrlHint, 0xFFE2E8F0);

    present();
}

void DisplayManager::drawStartingSession(const GameTile& game,
                                         const std::string& statusMsg,
                                         uint64_t frameCount,
                                         bool cancelModalOpen,
                                         int cancelModalBtnIdx) {
    if (!isReady_) return;

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

    drawStaticScene();

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

void DisplayManager::drawPostSessionModal(const std::vector<GameTile>& suggestions,
                                         int selectedSuggestionIdx,
                                         uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    clear(0xFF0E0B16);

    int mw = 1440, mh = 640;
    int mx = (1920 - mw) / 2, my = (1080 - mh) / 2;
    drawRoundedRect(mx, my, mw, mh, 20, 0xFF140F22);
    drawRoundedBorder(mx, my, mw, mh, 20, 2, 0xFF9146FF);

    drawText(mx + 60, my + 45, 4, "Juegos divertidos para seguir jugando...", 0xFFFFFFFF);
    drawText(mx + 60, my + 92, 2, "Recomendaciones basadas en tu sesion en Amazon Luna", 0xFF94A3B8);

    int cW = 220, cH = 310, cGap = 24;
    int startX = mx + 60;
    int startY = my + 145;

    int numSug = std::min(static_cast<int>(suggestions.size()), 5);
    for (int i = 0; i < numSug; ++i) {
        int cx = startX + i * (cW + cGap);
        bool isFoc = (selectedSuggestionIdx == i);

        const auto& sTile = suggestions[i];
        auto cImg = luna::util::ImageLoader::instance().getImage(sTile.coverUrl, luna::util::ImagePreset::VerticalPoster);
        if (cImg && cImg->ready) {
            drawPreScaledImage(cx, startY, cImg->rgba.data(), cW, cH, cImg->width, cImg->height);
        } else {
            drawRoundedRect(cx, startY, cW, cH, 12, 0xFF241B3B);
        }
        if (isFoc) {
            drawRoundedBorder(cx - 2, startY - 2, cW + 4, cH + 4, 14, 3, 0xFFFFFFFF);
        }
        drawText(cx, startY + cH + 16, 2, utf8SafeTruncate(sTile.title, 18), 0xFFFFFFFF);
    }

    drawPillButton(mx + (mw - 320) / 2, my + mh - 90, 320, 56, "[(O) Volver al inicio]", true, "", false);
    present();
}

void DisplayManager::drawFaqModal(const std::string& faqQuestion,
                                 const std::string& faqAnswer,
                                 uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    blendRectUniform(0, 0, 1920, 1080, 0xFF000000, 0xD0);

    int mw = 1200, mh = 520;
    int mx = (1920 - mw) / 2, my = (1080 - mh) / 2;
    drawRoundedRect(mx, my, mw, mh, 16, 0xFF181226);
    drawRoundedBorder(mx, my, mw, mh, 16, 2, 0xFF9146FF);

    drawText(mx + 60, my + 45, 4, faqQuestion, 0xFFFFFFFF);
    fillRect(mx + 60, my + 95, mw - 120, 1, 0xFF352654);

    auto ansLines = utf8WrapLines(faqAnswer, 85, 8);
    int ay = my + 125;
    for (const auto& al : ansLines) {
        drawText(mx + 60, ay, 2, al, 0xFFCBD5E1);
        ay += 32;
    }

    drawPillButton(mx + (mw - 340) / 2, my + mh - 90, 340, 56, "[(X) Entendido / Cerrar]", true, "", false);
    present();
}

void DisplayManager::drawBluetoothGamepadModal(uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    blendRectUniform(0, 0, 1920, 1080, 0xFF000000, 0xD0);

    int mw = 1000, mh = 480;
    int mx = (1920 - mw) / 2, my = (1080 - mh) / 2;
    drawRoundedRect(mx, my, mw, mh, 16, 0xFF181226);
    drawRoundedBorder(mx, my, mw, mh, 16, 2, 0xFF9146FF);

    drawText(mx + 50, my + 40, 4, "Emparejar Mando Amazon Luna por Bluetooth", 0xFFFFFFFF);
    fillRect(mx + 50, my + 85, mw - 100, 1, 0xFF9146FF);

    drawText(mx + 50, my + 120, 2, "1. Asegurate de que el mando Luna tiene pilas nuevas o bateria cargada.", 0xFFCBD5E1);
    drawText(mx + 50, my + 165, 2, "2. Manten pulsados el boton de accion (circulo) y el boton de inicio durante 3 segundos.", 0xFFCBD5E1);
    drawText(mx + 50, my + 210, 2, "3. El anillo luminoso naranja empezara a parpadear en modo emparejamiento.", 0xFFCBD5E1);
    drawText(mx + 50, my + 255, 2, "4. Tu consola PS5 o pantalla detectara el mando automaticamente.", 0xFF10B981);

    drawPillButton(mx + (mw - 280) / 2, my + mh - 90, 280, 56, "[(X) Entendido]", true, "", false);
    present();
}

void DisplayManager::present() {
    if (!isReady_) return;
    if (deferPresent_) {
        deferPresent_ = false;
        return;
    }

#if defined(LUNA_TARGET_PS5)
    if (!mappedMemory_) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);

    const TileOrder& order = getTileOrder();
    constexpr unsigned kTotalTiles = kTilesX * kTilesY;

    for (unsigned t = 0; t < kTotalTiles; ++t) {
        unsigned tx = (t % kTilesX) * 128;
        unsigned ty = (t / kTilesX) * 128;
        auto* out = reinterpret_cast<uint32_t*>(base + static_cast<size_t>(t) * 0x10000);

        if (tx + 128 <= kWidth && ty + 128 <= kHeight) {
            for (unsigned i = 0; i < 16384; ++i) {
                unsigned x = tx + order.dx[i];
                unsigned y = ty + order.dy[i];
                out[i] = canvas_[y * kWidth + x];
            }
        } else {
            for (unsigned i = 0; i < 16384; ++i) {
                unsigned x = tx + order.dx[i];
                unsigned y = ty + order.dy[i];
                out[i] = (x < kWidth && y < kHeight) ? canvas_[y * kWidth + x] : 0xFF000000U;
            }
        }
    }

    flushCacheRange(base, kUsedBytes);
    flipMarker_++;
    sceVideoOutSubmitFlip(videoHandle_, currentBuffer_, 1, flipMarker_);
#else
    if (mappedMemory_) {
        std::memcpy(mappedMemory_, canvas_, width_ * height_ * sizeof(uint32_t));
    }
#endif

    currentBuffer_ = (currentBuffer_ + 1) % kBuffers;
}

} // namespace luna::display
