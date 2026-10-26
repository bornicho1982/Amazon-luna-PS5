#include "display/display_manager.h"
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
    fontRenderer_.init();
    loadStaticHeroBanner();
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

void DisplayManager::setPixelSwizzled(uint8_t* base, int x, int y, uint32_t colorArgb) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    uint32_t abgr = argbToAbgr(colorArgb);
#if defined(LUNA_TARGET_PS5)
    size_t offset = tiledByteOffset(static_cast<unsigned>(x), static_cast<unsigned>(y));
    *reinterpret_cast<uint32_t*>(base + offset) = abgr;
#else
    reinterpret_cast<uint32_t*>(base)[y * width_ + x] = abgr;
#endif
}

uint32_t DisplayManager::getPixelSwizzled(const uint8_t* base, int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
#if defined(LUNA_TARGET_PS5)
    size_t offset = tiledByteOffset(static_cast<unsigned>(x), static_cast<unsigned>(y));
    return *reinterpret_cast<const uint32_t*>(base + offset);
#else
    return reinterpret_cast<const uint32_t*>(base)[y * width_ + x];
#endif
}

void DisplayManager::blendPixelSwizzled(uint8_t* base, int x, int y, uint32_t colorArgb, uint8_t alpha) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || alpha == 0) return;

    uint32_t srcA = (((colorArgb >> 24) & 0xFF) * alpha) / 255;
    if (srcA == 0) return;
    if (srcA >= 255) {
        setPixelSwizzled(base, x, y, colorArgb);
        return;
    }

#if defined(LUNA_TARGET_PS5)
    size_t offset = tiledByteOffset(static_cast<unsigned>(x), static_cast<unsigned>(y));
    uint32_t dstAbgr = *reinterpret_cast<uint32_t*>(base + offset);
#else
    uint32_t dstAbgr = reinterpret_cast<uint32_t*>(base)[y * width_ + x];
#endif

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
    uint32_t outAbgr = 0xFF000000U | (outB << 16) | (outG << 8) | outR;

#if defined(LUNA_TARGET_PS5)
    *reinterpret_cast<uint32_t*>(base + offset) = outAbgr;
#else
    reinterpret_cast<uint32_t*>(base)[y * width_ + x] = outAbgr;
#endif
}

void DisplayManager::clear(uint32_t colorArgb) {
    if (!isReady_ || !mappedMemory_) return;

    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    uint32_t abgr = argbToAbgr(colorArgb);

#if defined(LUNA_TARGET_PS5)
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
#else
    auto* buf = reinterpret_cast<uint32_t*>(base);
    for (int i = 0; i < width_ * height_; ++i) {
        buf[i] = abgr;
    }
#endif
}

void DisplayManager::fillRect(int rx, int ry, int rw, int rh, uint32_t colorArgb) {
    if (!isReady_ || !mappedMemory_) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    int x0 = rx < 0 ? 0 : rx;
    int y0 = ry < 0 ? 0 : ry;
    int x1 = (rx + rw) > width_ ? width_ : (rx + rw);
    int y1 = (ry + rh) > height_ ? height_ : (ry + rh);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            setPixelSwizzled(base, x, y, colorArgb);
        }
    }
}

void DisplayManager::drawImage(int rx, int ry, int rw, int rh, const uint8_t* rgba, int imgW, int imgH) {
    if (!isReady_ || !mappedMemory_ || !rgba || imgW <= 0 || imgH <= 0 || rw <= 0 || rh <= 0) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);

    int x0 = rx < 0 ? 0 : rx;
    int y0 = ry < 0 ? 0 : ry;
    int x1 = (rx + rw) > width_ ? width_ : (rx + rw);
    int y1 = (ry + rh) > height_ ? height_ : (ry + rh);

    for (int y = y0; y < y1; ++y) {
        int srcY = ((y - ry) * imgH) / rh;
        if (srcY >= imgH) srcY = imgH - 1;
        const uint8_t* row = rgba + (static_cast<size_t>(srcY) * static_cast<size_t>(imgW) * 4);
        for (int x = x0; x < x1; ++x) {
            int srcX = ((x - rx) * imgW) / rw;
            if (srcX >= imgW) srcX = imgW - 1;
            const uint8_t* p = row + (srcX * 4);
            uint8_t r = p[0];
            uint8_t g = p[1];
            uint8_t b = p[2];
            uint8_t a = p[3];
            uint32_t argb = (static_cast<uint32_t>(a) << 24) |
                            (static_cast<uint32_t>(r) << 16) |
                            (static_cast<uint32_t>(g) << 8)  |
                            static_cast<uint32_t>(b);
            if (a == 255) {
                setPixelSwizzled(base, x, y, argb);
            } else if (a > 0) {
                blendPixelSwizzled(base, x, y, argb, a);
            }
        }
    }
}


void DisplayManager::drawPreScaledImage(int rx, int ry, const uint8_t* rgba, int dstW, int dstH, int srcW, int srcH) {
    if (!isReady_ || !mappedMemory_ || !rgba || dstW <= 0 || dstH <= 0) return;
    if (srcW <= 0) srcW = dstW;
    if (srcH <= 0) srcH = dstH;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);

    int x0 = std::max(0, rx);
    int y0 = std::max(0, ry);
    int x1 = std::min(width_, rx + dstW);
    int y1 = std::min(height_, ry + dstH);

    float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);

    for (int y = y0; y < y1; ++y) {
        int srcY = std::clamp(static_cast<int>((y - ry) * scaleY), 0, srcH - 1);
        const uint8_t* row = rgba + (static_cast<size_t>(srcY) * static_cast<size_t>(srcW) * 4);
        for (int x = x0; x < x1; ++x) {
            int srcX = std::clamp(static_cast<int>((x - rx) * scaleX), 0, srcW - 1);
            const uint8_t* p = row + (srcX * 4);
            uint8_t a = p[3];
            if (a == 255) {
                uint32_t argb = 0xFF000000U | (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
                setPixelSwizzled(base, x, y, argb);
            } else if (a > 0) {
                uint32_t argb = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
                blendPixelSwizzled(base, x, y, argb, a);
            }
        }
    }
}

void DisplayManager::drawPlayTriangleIcon(int cx, int cy, int size, uint32_t color) {
    if (!isReady_ || !mappedMemory_ || size <= 0) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
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
            setPixelSwizzled(base, x, y, color);
        }
    }
}

void DisplayManager::drawStarIcon(int cx, int cy, int size, uint32_t color) {
    if (!isReady_ || !mappedMemory_ || size <= 0) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
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
                setPixelSwizzled(base, x, y, color);
            }
        }
    }
}

void DisplayManager::drawLunaLogoIcon(int x, int y, int size) {
    if (!isReady_ || !mappedMemory_ || size <= 0) return;
    int s2 = size * 2 / 3;
    drawRoundedRect(x, y + 4, s2, s2, 5, 0xFF02FDDB);
    drawRoundedRect(x + size / 3, y, s2, s2, 5, 0xFF9146FF);
}

void DisplayManager::drawRoundedRect(int x, int y, int w, int h, int r, uint32_t colorArgb) {
    if (!isReady_ || !mappedMemory_ || w <= 0 || h <= 0) return;
    if (r <= 0) {
        fillRect(x, y, w, h, colorArgb);
        return;
    }
    r = std::min(r, std::min(w / 2, h / 2));
    fillRect(x, y + r, w, h - (2 * r), colorArgb);
    fillRect(x + r, y, w - (2 * r), r, colorArgb);
    fillRect(x + r, y + h - r, w - (2 * r), r, colorArgb);

    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
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
                blendPixelSwizzled(base, x + cx, y + cy, colorArgb, a);
                blendPixelSwizzled(base, x + w - 1 - cx, y + cy, colorArgb, a);
                blendPixelSwizzled(base, x + cx, y + h - 1 - cy, colorArgb, a);
                blendPixelSwizzled(base, x + w - 1 - cx, y + h - 1 - cy, colorArgb, a);
            }
        }
    }
}

void DisplayManager::drawRoundedBorder(int x, int y, int w, int h, int r, int thickness, uint32_t colorArgb) {
    if (!isReady_ || !mappedMemory_ || thickness <= 0 || w <= 0 || h <= 0) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
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
                blendPixelSwizzled(base, x + cx, y + cy, colorArgb, a);
                blendPixelSwizzled(base, x + w - 1 - cx, y + cy, colorArgb, a);
                blendPixelSwizzled(base, x + cx, y + h - 1 - cy, colorArgb, a);
                blendPixelSwizzled(base, x + w - 1 - cx, y + h - 1 - cy, colorArgb, a);
            }
        }
    }
}

void DisplayManager::drawPillButton(int x, int y, int w, int h, const std::string& text, bool focused, const std::string& subtext, bool hasPlayIcon) {
    int r = h / 2;
    uint32_t bgColor = focused ? 0xFFFFFFFF : 0xFF353047;
    uint32_t fgColor = focused ? 0xFF0E0B16 : 0xFFFFFFFF;
    uint32_t subColor = focused ? 0xFF353047 : 0xFF94A3B8;

    drawRoundedRect(x, y, w, h, r, bgColor);

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
} // namespace

void DisplayManager::drawText(int x, int y, int scale, const std::string& text, uint32_t colorArgb) {
    if (!isReady_ || !mappedMemory_ || scale <= 0 || text.empty()) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);

    int fontSize = 24;
    switch (scale) {
        case 1: fontSize = 16; break;
        case 2: fontSize = 20; break;
        case 3: fontSize = 26; break;
        case 4: fontSize = 34; break;
        case 5: fontSize = 44; break;
        case 6: fontSize = 54; break;
        case 7: fontSize = 66; break;
        case 8: fontSize = 80; break;
        default: fontSize = scale * 9; break;
    }

    int cursorX = x;
    int cursorY = y;
    int lineHeight = fontSize + (fontSize / 3);
    int baseline = static_cast<int>(fontSize * 0.82f);

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

        // Simbolos especiales de marca registrada con fallback si la fuente no los incluye
        if (codepoint == 0x2122) { // ™
            const auto& gT = fontRenderer_.getGlyph('T', fontSize * 3 / 4);
            const auto& gM = fontRenderer_.getGlyph('M', fontSize * 3 / 4);
            if (gT.advance > 0) {
                int gy = cursorY + (baseline / 2);
                for (int r = 0; r < gT.height; ++r) {
                    for (int c = 0; c < gT.width; ++c) {
                        uint8_t a = gT.bitmap[r * gT.width + c];
                        if (a > 0) blendPixelSwizzled(base, cursorX + gT.xoff + c, gy + gT.yoff + r, colorArgb, a);
                    }
                }
                cursorX += gT.advance;
                for (int r = 0; r < gM.height; ++r) {
                    for (int c = 0; c < gM.width; ++c) {
                        uint8_t a = gM.bitmap[r * gM.width + c];
                        if (a > 0) blendPixelSwizzled(base, cursorX + gM.xoff + c, gy + gM.yoff + r, colorArgb, a);
                    }
                }
                cursorX += gM.advance + 2;
                continue;
            }
        }

        const auto& glyph = fontRenderer_.getGlyph(codepoint, fontSize);

        int gx = cursorX + glyph.xoff;
        int gy = cursorY + baseline + glyph.yoff;

        for (int r = 0; r < glyph.height; ++r) {
            for (int c = 0; c < glyph.width; ++c) {
                uint8_t a = glyph.bitmap[r * glyph.width + c];
                if (a > 0) {
                    blendPixelSwizzled(base, gx + c, gy + r, colorArgb, a);
                }
            }
        }
        cursorX += (glyph.advance > 0) ? glyph.advance : (fontSize / 2);
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
                                        int searchLetterIdx,
                                        int searchGridIdx,
                                        const std::vector<GameTile>& filteredGames) {
    if (!isReady_ || !mappedMemory_) return;
    (void)heroBtnIdx; (void)frameCount;

    // 1. Fondo principal oficial de Amazon Luna (#0E0B16 Cosmic Deep)
    clear(0xFF0E0B16);

    if (collections.empty()) {
        drawText(68, 500, 3, "Cargando catalogo de Amazon Luna...", 0xFF94A3B8);
        present();
        return;
    }

    int clampedRow = std::clamp(selectedRowIdx, 0, static_cast<int>(collections.size()) - 1);
    bool hasHero = collections[0].isHeroRotator && !collections[0].items.empty();

    // Desplazamiento vertical de filas cuando se navega hacia abajo
    int yOffset = 0;
    if (clampedRow > 1) {
        yOffset = (clampedRow - 1) * 360;
    }

    // 2. HERO BANNER PANORAMICO 16:9
    if (hasHero) {
        const auto& heroCol = collections[0];
        int heroIdx = (!rowSelectedCol.empty()) ? rowSelectedCol[0] : 0;
        if (heroIdx < 0 || heroIdx >= static_cast<int>(heroCol.items.size())) heroIdx = 0;
        const auto& heroGame = heroCol.items[heroIdx];
        bool heroFocused = (focusZone != 0 && clampedRow == 0);

        int heroY = 0 - yOffset;
        if (heroY + 560 > 0) {
            // Fondo panoramico que ocupa todo el ancho superior con degradado suave a #0E0B16
            std::string heroBackdrop = heroGame.backdropUrl;
            if (heroBackdrop.empty()) {
                for (const auto& h : heroCol.items) {
                    if (!h.backdropUrl.empty()) { heroBackdrop = h.backdropUrl; break; }
                }
            }
            if (heroBackdrop.empty() && collections.size() > 1) {
                for (const auto& it : collections[1].items) {
                    if (!it.backdropUrl.empty()) { heroBackdrop = it.backdropUrl; break; }
                }
            }

            auto heroImg = luna::util::ImageLoader::instance().getImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
            if (heroImg && heroImg->ready) {
                drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
            } else if (!heroBannerRgba_.empty()) {
                drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
            } else {
                if (!heroBackdrop.empty()) {
                    luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                }
                fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
            }

            // Lado izquierdo del Hero (x = 68):
            // A. Logo transparente PNG (productIcon) o titulo en grande
            bool drawnLogo = false;
            if (!heroGame.logoUrl.empty()) {
                auto logoImg = luna::util::ImageLoader::instance().getImage(heroGame.logoUrl, luna::util::ImagePreset::ProductLogo);
                if (logoImg && logoImg->ready) {
                    drawPreScaledImage(68, heroY + 115, logoImg->rgba.data(), logoImg->width, logoImg->height, logoImg->width, logoImg->height);
                    drawnLogo = true;
                } else {
                    luna::util::ImageLoader::instance().requestImage(heroGame.logoUrl, luna::util::ImagePreset::ProductLogo);
                }
            }
            if (!drawnLogo) {
                drawText(68, heroY + 140, 6, utf8SafeTruncate(heroGame.title, 26), 0xFFFFFFFF);
            }

            // B. Subtitulo limpio
            std::string sub = heroGame.subtitle.empty() ? "Mas de 100 juegos. Una suscripcion." : heroGame.subtitle;
            drawText(68, heroY + 310, 2, utf8SafeTruncate(sub, 54), 0xFFE2E8F0);

            // C. Boton redondeado (#FFFFFF / texto #0E0B16 cuando enfocado)
            int btnW = 280;
            int btnH = 58;
            std::string btnTxt = heroGame.buttonText.empty() ? "Mas informacion" : heroGame.buttonText;
            drawPillButton(68, heroY + 365, btnW, btnH, btnTxt, heroFocused);

            // D. Indicador de carrusel (dots)
            int totalSlides = std::min(static_cast<int>(heroCol.items.size()), 6);
            int dotX = 68;
            int dotY = heroY + 450;
            for (int s = 0; s < totalSlides; ++s) {
                if (s == heroIdx) {
                    drawRoundedRect(dotX, dotY, 28, 6, 3, 0xFFFFFFFF);
                    dotX += 36;
                } else {
                    drawRoundedRect(dotX, dotY, 6, 6, 3, 0x8894A3B8);
                    dotX += 14;
                }
            }
        }
    }

    // 3. SECCION DE SWIMLANES / FILAS HORIZONTALES
    int firstRowIdx = hasHero ? 1 : 0;
    int baseRowY = (hasHero ? 515 : 100) - yOffset;
    int cardW = 200;
    int cardH = 280;

    for (size_t r = firstRowIdx; r < collections.size(); ++r) {
        int rowY = baseRowY + static_cast<int>(r - firstRowIdx) * 360;
        if (rowY + 360 < 0 || rowY > 1080) continue;

        const auto& row = collections[r];
        bool isFocusedRow = (focusZone != 0 && static_cast<int>(r) == clampedRow);
        int selectedCol = (r < rowSelectedCol.size()) ? rowSelectedCol[r] : 0;

        // Titulo limpio de la fila en blanco
        drawText(68, rowY + 10, 3, row.title, 0xFFFFFFFF);

        // Desplazamiento horizontal suave y margen amplio para rankings oficiales
        int curCardGap = 28;
        int rankMargin = row.isRankings ? 72 : 0;
        int slotWidth = cardW + curCardGap + rankMargin;
        int scrollOffset = std::max(0, (selectedCol - (row.isRankings ? 3 : 3)) * slotWidth);

        for (size_t i = 0; i < row.items.size(); ++i) {
            int slotX = 68 + static_cast<int>(i) * slotWidth - scrollOffset;
            int numberX = slotX;
            int cardX = slotX + rankMargin;
            if (cardX + cardW < 0 || numberX > 1920) continue;

            const auto& item = row.items[i];
            bool isSelectedCard = isFocusedRow && (static_cast<int>(i) == selectedCol);
            int cardY = rowY + 50;

            if (row.isRankings) {
                std::string rankStr = std::to_string(i + 1);
                // Dibuja el número gigante con tipografía estilizada y color 0x24FFFFFF
                drawText(numberX, cardY + cardH - 28, 8, rankStr, 0x24FFFFFF);
            }

            auto coverImg = luna::util::ImageLoader::instance().getImage(item.coverUrl, luna::util::ImagePreset::VerticalPoster);
            if (coverImg && coverImg->ready) {
                drawPreScaledImage(cardX, cardY, coverImg->rgba.data(), 200, 280, coverImg->width, coverImg->height);
            } else {
                if (!item.coverUrl.empty()) {
                    luna::util::ImageLoader::instance().requestImage(item.coverUrl, luna::util::ImagePreset::VerticalPoster);
                }
                drawRoundedRect(cardX, cardY, cardW, cardH, 14, 0xFF241B3B);
                drawText(cardX + 16, cardY + 110, 2, utf8SafeTruncate(item.title, 18), 0xFFFFFFFF);
            }

            if (isSelectedCard) {
                drawRoundedBorder(cardX - 2, cardY - 2, cardW + 4, cardH + 4, 16, 3, 0xFFFFFFFF);
            }
        }
    }

    // 4. BARRA DE NAVEGACION SUPERIOR
    fillRect(0, 0, 1920, 68, 0xCC0E0B16);
    drawText(44, 22, 2, "<", 0xFF94A3B8);
    drawText(74, 22, 2, ">", 0xFF94A3B8);
    drawLunaLogoIcon(112, 22, 22);
    drawText(142, 20, 3, "luna", 0xFFFFFFFF);

    int tabX = 230;
    int numTabs = static_cast<int>(navTabs.size());
    for (int t = 0; t < numTabs; ++t) {
        bool active = (t == activeTabIdx);
        bool focusedTab = (focusZone == 0 && navColIdx == t);

        uint32_t tColor = (active || focusedTab) ? 0xFFFFFFFF : 0xFF94A3B8;
        drawText(tabX, 22, 2, navTabs[t], tColor);
        int tw = fontRenderer_.measureText(navTabs[t], 20);

        if (focusedTab) {
            drawRoundedBorder(tabX - 8, 14, tw + 16, 40, 8, 2, 0xFF9146FF);
        } else if (active) {
            fillRect(tabX, 52, tw, 3, 0xFFFFFFFF);
        }
        tabX += tw + 32;
    }

    int magX = 1754, magY = 32, magR = 8;
    bool magFocused = (focusZone == 0 && navColIdx == numTabs);
    if (magFocused) {
        drawRoundedBorder(magX - 16, 14, 38, 38, 8, 2, 0xFFFFFFFF);
    }
    drawRoundedBorder(magX - magR, magY - magR, magR * 2, magR * 2, magR, 2, magFocused ? 0xFFFFFFFF : 0xFFCBD5E1);
    for (int d = 0; d < 6; ++d) {
        fillRect(magX + magR - 3 + d, magY + magR - 3 + d, 2, 2, magFocused ? 0xFFFFFFFF : 0xFFCBD5E1);
    }

    int profX = 1810, profY = 16;
    bool profFocused = (focusZone == 0 && navColIdx == numTabs + 1);
    if (profFocused) {
        drawRoundedBorder(profX - 3, profY - 3, 44, 44, 22, 3, 0xFFFFFFFF);
    }
    drawRoundedRect(profX, profY, 38, 38, 19, 0xFF14B8A6);
    drawText(profX + 12, profY + 8, 2, "B", 0xFFFFFFFF);

    // 5. BARRA DE ESTADO INFERIOR
    if (!statusText.empty()) {
        fillRect(0, 1046, 1920, 34, 0xEE0B0912);
        drawText(44, 1054, 1, statusText, 0xFF94A3B8);
        drawText(1700, 1054, 1, "DualSense conectado", padConnected ? 0xFF10B981 : 0xFFEF4444);
    }

    // 6. POPUP MENU PERFIL DE USUARIO
    if (profileMenuOpen) {
        int pw = 450, ph = 260;
        int px = 1920 - pw - 40;
        int py = 76;
        drawRoundedRect(px, py, pw, ph, 14, 0xF5140F24);
        drawRoundedBorder(px, py, pw, ph, 14, 2, 0xFF9146FF);

        drawRoundedRect(px + 20, py + 20, 42, 42, 21, 0xFF14B8A6);
        drawText(px + 34, py + 29, 2, "B", 0xFFFFFFFF);
        drawText(px + 74, py + 22, 2, profile.displayName + " " + profile.discriminator, 0xFFFFFFFF);
        drawText(px + 74, py + 46, 1, profile.planStatus, 0xFF10B981);

        fillRect(px + 20, py + 74, pw - 40, 1, 0xFF2D2545);

        const char* menuOpts[3] = {
            "Recargar catalogo de Amazon Luna",
            "Cerrar sesion de Amazon (Cambiar cuenta)",
            "Cerrar menu"
        };
        for (int m = 0; m < 3; ++m) {
            int by = py + 86 + m * 50;
            bool btnFoc = (profileMenuIdx == m);
            if (btnFoc) {
                drawRoundedRect(px + 16, by, pw - 32, 42, 8, 0xFF9146FF);
                drawText(px + 32, by + 12, 1, menuOpts[m], 0xFFFFFFFF);
            } else {
                drawText(px + 32, by + 12, 1, menuOpts[m], 0xFFCBD5E1);
            }
        }
    }

    // 7. VISTA DE BUSQUEDA Y FILTRADO RAPIDO
    if (searchOpen) {
        fillRect(0, 0, 1920, 1080, 0xF80B0912);
        drawRoundedRect(68, 28, 1784, 54, 10, 0xFF1C152E);
        drawRoundedBorder(68, 28, 1784, 54, 10, 2, 0xFF9146FF);
        drawText(90, 44, 2, "FILTRAR CATALOGO POR LETRA / BUSQUEDA RAPIDA", 0xFFFFFFFF);
        drawText(1600, 46, 1, "(O) Volver al inicio", 0xFF94A3B8);

        static const char* kFilterLetters[] = {
            "TODOS", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
            "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "#"
        };
        int letX = 68;
        int letY = 96;
        for (int l = 0; l < 28; ++l) {
            bool letFoc = (searchLetterIdx == l);
            int lw = fontRenderer_.measureText(kFilterLetters[l], 18) + 16;
            if (letFoc) {
                drawRoundedRect(letX, letY, lw, 36, 6, 0xFF9146FF);
                drawText(letX + 8, letY + 8, 2, kFilterLetters[l], 0xFFFFFFFF);
            } else {
                drawText(letX + 8, letY + 8, 2, kFilterLetters[l], 0xFF94A3B8);
            }
            letX += lw + 6;
        }

        int gBaseX = 68;
        int gBaseY = 150;
        int gCardW = 200;
        int gCardH = 280;
        int gGap = 24;
        int numFiltered = static_cast<int>(filteredGames.size());
        for (int g = 0; g < numFiltered && g < 12; ++g) {
            int col = g % 6;
            int row = g / 6;
            int gx = gBaseX + col * (gCardW + gGap);
            int gy = gBaseY + row * (gCardH + 110);
            bool gFoc = (searchGridIdx == g);

            const auto& tile = filteredGames[g];
            auto cImg = luna::util::ImageLoader::instance().getImage(tile.coverUrl, luna::util::ImagePreset::VerticalPoster);
            if (cImg && cImg->ready) {
                drawPreScaledImage(gx, gy, cImg->rgba.data(), gCardW, gCardH, cImg->width, cImg->height);
            } else {
                drawRoundedRect(gx, gy, gCardW, gCardH, 12, 0xFF241B3B);
            }
            if (gFoc) {
                drawRoundedBorder(gx - 2, gy - 2, gCardW + 4, gCardH + 4, 14, 3, 0xFFFFFFFF);
            }
            drawText(gx, gy + 290, 2, utf8SafeTruncate(tile.title, 18), 0xFFFFFFFF);
            drawText(gx, gy + 316, 1, utf8SafeTruncate(tile.genres, 22), 0xFF94A3B8);
        }
    }

    present();
}

void DisplayManager::drawGameDetailScreen(const GameTile& game,
                                          const PlayerProfileInfo& profile,
                                          const std::string& statusText,
                                          bool isConnecting,
                                          int detailZone,
                                          int detailBtnIdx,
                                          int similarSelectedIdx,
                                          uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    // 1. Fondo principal oficial
    clear(0xFF0E0B16);

    // 2. Fondo panoramico 16:9 completo con degradado
    auto bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
    if (bgImg && bgImg->ready) {
        drawPreScaledImage(0, 0, bgImg->rgba.data(), 1920, 1080, bgImg->width, bgImg->height);
    } else {
        if (!game.backdropUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
        } else if (!game.coverUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(game.coverUrl, luna::util::ImagePreset::PanoramicDetail);
        }
        fillRect(0, 0, 1920, 1080, 0xFF140F24);
    }

    // Barra superior: < Volver
    fillRect(0, 0, 1920, 68, 0x990E0B16);
    drawText(44, 22, 2, "<  Volver", 0xFFE2E8F0);
    int magX = 1754, magY = 32, magR = 8;
    drawRoundedBorder(magX - magR, magY - magR, magR * 2, magR * 2, magR, 2, 0xFFCBD5E1);
    for (int d = 0; d < 6; ++d) {
        fillRect(magX + magR - 3 + d, magY + magR - 3 + d, 2, 2, 0xFFCBD5E1);
    }
    drawRoundedRect(1810, 16, 38, 38, 19, 0xFF14B8A6);
    drawText(1822, 24, 2, "B", 0xFFFFFFFF);

    // 3. Lado izquierdo (x = 68): Ficha del Juego
    bool drawnLogo = false;
    if (!game.logoUrl.empty()) {
        auto logoImg = luna::util::ImageLoader::instance().getImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
        if (logoImg && logoImg->ready) {
            drawPreScaledImage(68, 135, logoImg->rgba.data(), logoImg->width, logoImg->height, logoImg->width, logoImg->height);
            drawnLogo = true;
        } else {
            luna::util::ImageLoader::instance().requestImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
        }
    }
    if (!drawnLogo) {
        drawText(68, 155, 6, utf8SafeTruncate(game.title, 26), 0xFFFFFFFF);
    }

    std::string genreText = game.genres.empty() ? "Accion, Aventura" : game.genres;
    drawText(68, 320, 2, genreText, 0xFFA0A0B0);

    // Boton primario (Jugar ahora con icono play)
    int btnW = 340;
    int btnH = 64;
    bool primFocused = (detailZone == 0 && detailBtnIdx == 0);

    if (game.isEntitled) {
        std::string label = isConnecting ? "CONECTANDO..." : "Jugar ahora";
        drawPillButton(68, 360, btnW, btnH, label, primFocused, "", true);
    } else {
        std::string pri = game.buttonText.empty() ? "Incluido en Luna Premium" : game.buttonText;
        std::string sec = game.secondaryButtonText.empty() ? "9,99 EUR/mes" : game.secondaryButtonText;
        drawPillButton(68, 360, btnW, btnH, pri, primFocused, sec);
    }

    // Boton de favoritos con estrella vectorial
    bool favFocused = (detailZone == 0 && detailBtnIdx == 1);
    drawRoundedRect(425, 360, 64, 64, 14, favFocused ? 0xFFFFFFFF : 0xFF353047);
    drawStarIcon(457, 392, 12, favFocused ? 0xFF0E0B16 : 0xFFFFFFFF);

    drawText(68, 448, 1, "Compartir    Anadir al escritorio    Informar de un problema", 0xFF94A3B8);

    std::string acctStr = "Cuenta vinculada: " + profile.displayName + " (" + profile.planStatus + ")";
    drawText(68, 474, 1, acctStr, 0xFF64748B);

    auto synLines = utf8WrapLines(game.synopsis, 80, 2);
    int synY = 504;
    for (const auto& l : synLines) {
        drawText(68, synY, 2, l, 0xFFCBD5E1);
        synY += 26;
    }

    // 4. Lado derecho: BADGE OFICIAL PEGI
    int pegiX = 1525, pegiY = 350, pegiW = 74, pegiH = 86;
    uint32_t pegiBg = 0xFFF59E0B;
    std::string pegiNum = "12";
    if (game.pegiRating.find("18") != std::string::npos) {
        pegiBg = 0xFFDC2626;
        pegiNum = "18";
    } else if (game.pegiRating.find("16") != std::string::npos) {
        pegiBg = 0xFFEA580C;
        pegiNum = "16";
    } else if (game.pegiRating.find("7") != std::string::npos || game.pegiRating.find("3") != std::string::npos) {
        pegiBg = 0xFF10B981;
        pegiNum = (game.pegiRating.find("7") != std::string::npos) ? "7" : "3";
    }

    drawRoundedRect(pegiX, pegiY, pegiW, pegiH, 8, pegiBg);
    drawRoundedBorder(pegiX, pegiY, pegiW, pegiH, 8, 2, 0xFF0E0B16);
    drawText(pegiX + 16, pegiY + 12, 1, "PEGI", 0xFF0E0B16);
    int pnw = fontRenderer_.measureText(pegiNum, 34);
    drawText(pegiX + (pegiW - pnw) / 2, pegiY + 36, 4, pegiNum, 0xFF0E0B16);
    drawText(pegiX + 8, pegiY + 70, 1, "www.pegi.info", 0xFF0E0B16);

    int descY = pegiY + 6;
    int descX = pegiX + pegiW + 16;
    if (!game.ageRatingDescriptors.empty()) {
        for (const auto& d : game.ageRatingDescriptors) {
            drawText(descX, descY, 1, d, 0xFFCBD5E1);
            descY += 22;
        }
    } else {
        drawText(descX, descY, 1, "Violencia moderada", 0xFFCBD5E1);
        drawText(descX, descY + 22, 1, "Palabras malsonantes leves", 0xFFCBD5E1);
    }

    // 5. Carrusel inferior: Si hay screenshots reales, mostrarlas
    int curY = 590;
    size_t numScreens = game.screenshotUrls.size();
    if (numScreens > 0) {
        drawText(68, curY, 3, "Trailer y capturas de pantalla", 0xFFFFFFFF);
        int thumbW = 270, thumbH = 152, thumbGap = 16;
        int thumbY = curY + 36;
        for (size_t t = 0; t < numScreens && t < 4; ++t) {
            int tx = 68 + static_cast<int>(t) * (thumbW + thumbGap);
            if (tx + thumbW > 1920) break;
            bool isTrailer = (t == 0);
            std::string imgUrl = game.screenshotUrls[t];
            auto thImg = luna::util::ImageLoader::instance().getImage(imgUrl, luna::util::ImagePreset::ScreenshotThumbnail);
            if (thImg && thImg->ready) {
                drawPreScaledImage(tx, thumbY, thImg->rgba.data(), thumbW, thumbH, thImg->width, thImg->height);
            } else {
                luna::util::ImageLoader::instance().requestImage(imgUrl, luna::util::ImagePreset::ScreenshotThumbnail);
                drawRoundedRect(tx, thumbY, thumbW, thumbH, 10, 0xFF241B3B);
            }
            if (isTrailer) {
                drawRoundedRect(tx + (thumbW - 54) / 2, thumbY + (thumbH - 54) / 2, 54, 54, 27, 0xCC0E0B16);
                drawPlayTriangleIcon(tx + thumbW / 2, thumbY + thumbH / 2, 12, 0xFFFFFFFF);
            }
            if (detailZone == 1 && similarSelectedIdx == static_cast<int>(t)) {
                drawRoundedBorder(tx - 2, thumbY - 2, thumbW + 4, thumbH + 4, 12, 3, 0xFFFFFFFF);
            }
        }
        curY += 210;
    }

    // Carrusel "Tambien te puede interesar"
    if (!game.similarGames.empty() && curY + 220 <= 1046) {
        drawText(68, curY, 3, "Tambien te puede interesar", 0xFFFFFFFF);
        int simCardW = 140, simCardH = 196, simGap = 20;
        int simCardsY = curY + 36;
        int simFocusZone = (numScreens > 0) ? 2 : 1;
        for (size_t s = 0; s < game.similarGames.size() && s < 8; ++s) {
            int sx = 68 + static_cast<int>(s) * (simCardW + simGap);
            if (sx + simCardW > 1920) break;
            const auto& simTile = game.similarGames[s];
            auto sImg = luna::util::ImageLoader::instance().getImage(simTile.coverUrl, luna::util::ImagePreset::VerticalPoster);
            if (sImg && sImg->ready) {
                drawPreScaledImage(sx, simCardsY, sImg->rgba.data(), simCardW, simCardH, sImg->width, sImg->height);
            } else {
                if (!simTile.coverUrl.empty()) {
                    luna::util::ImageLoader::instance().requestImage(simTile.coverUrl, luna::util::ImagePreset::VerticalPoster);
                }
                drawRoundedRect(sx, simCardsY, simCardW, simCardH, 10, 0xFF241B3B);
                drawText(sx + 8, simCardsY + 80, 1, utf8SafeTruncate(simTile.title, 14), 0xFFFFFFFF);
            }
            bool isSimFoc = (detailZone == simFocusZone && similarSelectedIdx == static_cast<int>(s));
            if (isSimFoc) {
                drawRoundedBorder(sx - 2, simCardsY - 2, simCardW + 4, simCardH + 4, 12, 3, 0xFFFFFFFF);
            }
        }
    }

    // 6. Barra de estado inferior
    if (!statusText.empty()) {
        fillRect(0, 1046, 1920, 34, 0xEE0B0912);
        drawText(44, 1054, 1, statusText, 0xFF94A3B8);
    }

    present();
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
    if (!isReady_ || !mappedMemory_ || !y || !u || !v) return false;

    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);

#if defined(LUNA_TARGET_PS5)
    const TileOrder& order = getTileOrder();
    constexpr unsigned kTotalTiles = kTilesX * kTilesY;

    for (unsigned t = 0; t < kTotalTiles; ++t) {
        unsigned tx = t % kTilesX;
        unsigned ty = t / kTilesX;
        auto* out = reinterpret_cast<uint32_t*>(base + static_cast<size_t>(t) * 0x10000);

        for (unsigned i = 0; i < 16384; ++i) {
            unsigned px = tx * 128 + order.dx[i];
            unsigned py = ty * 128 + order.dy[i];
            if (px >= kWidth || py >= kHeight || static_cast<int>(px) >= width || static_cast<int>(py) >= height) {
                out[i] = 0xFF000000U;
                continue;
            }

            int yVal = y[py * strideY + px];
            int uVal = u[(py / 2) * strideU + (px / 2)] - 128;
            int vVal = v[(py / 2) * strideV + (px / 2)] - 128;

            int c = (yVal - 16) * 1192;
            uint32_t r = clamp8((c + 1836 * vVal + 512) >> 10);
            uint32_t g = clamp8((c - 218 * uVal - 546 * vVal + 512) >> 10);
            uint32_t b = clamp8((c + 2163 * uVal + 512) >> 10);

            out[i] = 0xFF000000U | (b << 16) | (g << 8) | r;
        }
    }
#else
    auto* dst = reinterpret_cast<uint32_t*>(base);
    for (int j = 0; j < height_ && j < height; ++j) {
        const uint8_t* yRow = y + (j * strideY);
        const uint8_t* uRow = u + ((j / 2) * strideU);
        const uint8_t* vRow = v + ((j / 2) * strideV);
        uint32_t* outRow = dst + (j * width_);

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
#endif

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

void DisplayManager::present() {
    if (!isReady_) return;

#if defined(LUNA_TARGET_PS5)
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    flushCacheRange(base, kUsedBytes);
    flipMarker_++;
    sceVideoOutSubmitFlip(videoHandle_, currentBuffer_, 1, flipMarker_);
#endif

    currentBuffer_ = (currentBuffer_ + 1) % kBuffers;
}

} // namespace luna::display
