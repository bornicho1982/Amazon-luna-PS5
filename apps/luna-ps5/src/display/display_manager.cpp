#include "display/display_manager.h"
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
    if (srcW > 4096 || srcH > 4096) return;
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);

    int x0 = std::clamp(rx, 0, width_);
    int y0 = std::clamp(ry, 0, height_);
    int x1 = std::clamp(rx + dstW, 0, width_);
    int y1 = std::clamp(ry + dstH, 0, height_);
    if (x0 >= x1 || y0 >= y1) return;

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
        case 8: fontSize = 110; break;
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
    (void)frameCount;

    // 1. Fondo principal oficial de Amazon Luna (#0E0B16 Cosmic Deep)
    clear(0xFF0E0B16);

    if (collections.empty() && activeTabIdx != 3) {
        drawText(68, 500, 3, "Cargando catalogo oficial de Amazon Luna...", 0xFF94A3B8);
        present();
        return;
    }

    // PESTANA 3: REMOTE PLAY
    if (activeTabIdx == 3) {
        // Fondo tematico Remote Play
        fillRect(0, 0, 1920, 1080, 0xFF0E0B16);
        drawRoundedRect(68, 100, 1784, 110, 16, 0xFF181226);
        drawRoundedBorder(68, 100, 1784, 110, 16, 1, 0xFF2D2248);

        drawText(100, 118, 5, "Remote Play", 0xFFFFFFFF);
        drawText(100, 168, 2, "Juega con tus amigos esteis donde esteis. Compartid partidas multijugador y cooperativas al instante sin coste adicional.", 0xFF94A3B8);

        int cardY = 240;
        int cardW = 860;
        int cardH = 500;

        // Tarjeta 1: Anfitrion (Crear una partida)
        int hostX = 68;
        bool hostFoc = (focusZone != 0 && (selectedRowIdx == 0 || selectedRowIdx == 1) && (!rowSelectedCol.empty() && rowSelectedCol[0] == 0));
        drawRoundedRect(hostX, cardY, cardW, cardH, 20, 0xFF140F22);
        drawRoundedBorder(hostX, cardY, cardW, cardH, 20, hostFoc ? 3 : 1, hostFoc ? 0xFFFFFFFF : 0xFF2D2248);

        drawRoundedRect(hostX + 40, cardY + 36, 140, 36, 8, 0xFF3B1B68);
        drawText(hostX + 56, cardY + 44, 2, "ANFITRION", 0xFFE9D5FF);
        drawText(hostX + 40, cardY + 92, 4, "Crear una partida", 0xFFFFFFFF);
        drawText(hostX + 40, cardY + 140, 2, "1. Genera un codigo unico de acceso de 6 digitos.", 0xFFCBD5E1);
        drawText(hostX + 40, cardY + 175, 2, "2. Enviaselo a tus amigos (hasta 8 jugadores).", 0xFFCBD5E1);
        drawText(hostX + 40, cardY + 210, 2, "3. Inicia cualquier juego compatible de tu biblioteca.", 0xFFCBD5E1);

        // Caja de codigo generado
        drawRoundedRect(hostX + 40, cardY + 265, cardW - 80, 90, 12, 0xFF22173B);
        drawRoundedBorder(hostX + 40, cardY + 265, cardW - 80, 90, 12, 1, 0xFF4C1D95);
        drawText(hostX + 60, cardY + 285, 2, "CODIGO DE ACCESO ACTUAL:", 0xFFA78BFA);
        drawText(hostX + 60, cardY + 312, 5, "2 9 9 9 8 3", 0xFFFCD34D);
        drawText(hostX + 380, cardY + 318, 2, "Sala activa lista para jugar", 0xFF10B981);

        drawPillButton(hostX + 40, cardY + 390, 380, 64, "Generar nuevo codigo", hostFoc, "", false);

        // Tarjeta 2: Invitado (Unirse a una partida)
        int guestX = 992;
        bool guestFoc = (focusZone != 0 && (selectedRowIdx == 0 || selectedRowIdx == 1) && (!rowSelectedCol.empty() && rowSelectedCol[0] == 1));
        drawRoundedRect(guestX, cardY, cardW, cardH, 20, 0xFF140F22);
        drawRoundedBorder(guestX, cardY, cardW, cardH, 20, guestFoc ? 3 : 1, guestFoc ? 0xFFFFFFFF : 0xFF2D2248);

        drawRoundedRect(guestX + 40, cardY + 36, 130, 36, 8, 0xFF064E3B);
        drawText(guestX + 56, cardY + 44, 2, "INVITADO", 0xFFA7F3D0);
        drawText(guestX + 40, cardY + 92, 4, "Unirse a una partida", 0xFFFFFFFF);
        drawText(guestX + 40, cardY + 140, 2, "Introduce el codigo de 6 digitos facilitado por el anfitrion:", 0xFFCBD5E1);

        // Cajas de digitos interactivos [ • ] [ • ] [ • ] [ • ] [ • ] [ • ]
        int pinBoxX = guestX + 40;
        int pinBoxY = cardY + 195;
        static const char* kDemoPinDigits[] = {"2", "9", "9", "9", "8", "3"};
        for (int p = 0; p < 6; ++p) {
            drawRoundedRect(pinBoxX, pinBoxY, 68, 76, 10, 0xFF22173B);
            drawRoundedBorder(pinBoxX, pinBoxY, 68, 76, 10, 2, guestFoc ? 0xFF9146FF : 0xFF374151);
            drawText(pinBoxX + 24, pinBoxY + 20, 4, kDemoPinDigits[p], 0xFFFFFFFF);
            pinBoxX += 82;
        }

        drawText(guestX + 40, cardY + 300, 2, "Pulsa (X) para validar el codigo e incorporarte a la sesion", 0xFF94A3B8);
        drawPillButton(guestX + 40, cardY + 390, 380, 64, "Unirme a la partida", guestFoc, "", true);

        // Barra inferior y navegacion
        drawText(68, 780, 3, "Juegos compatibles con Remote Play en tu suscripcion:", 0xFFFFFFFF);
        int rpGidY = 825;
        static const char* kRpTitles[] = {"The Game of Life 2", "Goat Simulator 3", "Cluedo", "Overcooked! 2", "Moving Out"};
        for (int rg = 0; rg < 5; ++rg) {
            int rx = 68 + rg * 350;
            drawRoundedRect(rx, rpGidY, 320, 90, 12, 0xFF1E1633);
            drawRoundedBorder(rx, rpGidY, 320, 90, 12, 1, 0xFF352654);
            drawText(rx + 20, rpGidY + 20, 2, kRpTitles[rg], 0xFFFFFFFF);
            drawText(rx + 20, rpGidY + 50, 1, "Multijugador local en nube", 0xFF10B981);
        }
    } else {
        int clampedRow = std::clamp(selectedRowIdx, 0, static_cast<int>(collections.size()) - 1);
        bool hasHero = (activeTabIdx != 2) && !collections.empty() && collections[0].isHeroRotator && !collections[0].items.empty();

        // Desplazamiento vertical de filas cuando se navega hacia abajo
        int yOffset = 0;
        if (clampedRow > 1) {
            yOffset = (clampedRow - 1) * 360;
        }

        // PESTANA 2: MI ESPACIO (SIN HERO BANNER, TITULO OFICIAL EN GRANDE)
        if (activeTabIdx == 2) {
            int titleY = 100 - yOffset;
            if (titleY > -40) {
                drawText(68, titleY, 6, "Mi espacio", 0xFFFFFFFF);
            }
        }

        // 2. HERO BANNER PANORAMICO 16:9 (PARA INICIO Y GAMENIGHT)
        if (hasHero) {
            const auto& heroCol = collections[0];
            int heroIdx = (!rowSelectedCol.empty()) ? rowSelectedCol[0] : 0;
            if (heroIdx < 0 || heroIdx >= static_cast<int>(heroCol.items.size())) heroIdx = 0;
            const auto& heroGame = heroCol.items[heroIdx];
            bool heroFocused = (focusZone != 0 && clampedRow == 0);

            int heroY = 0 - yOffset;
            if (heroY + 560 > 0) {
                std::string heroBackdrop = heroGame.backdropUrl.empty() ? heroGame.coverUrl : heroGame.backdropUrl;
                bool isFirstSlide = (heroCol.isHeroRotator && heroIdx == 0 && heroGame.title == "Luna Premium");
                if (isFirstSlide) {
                    if (!heroBannerRgba_.empty()) {
                        drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
                    } else {
                        fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                    }
                } else {
                    auto heroImg = luna::util::ImageLoader::instance().getImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                    if (heroImg && heroImg->ready) {
                        drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
                    } else {
                        if (!heroBackdrop.empty()) {
                            luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                        }
                        fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                    }
                }

                // Degradado oscuro suave sobre el Hero Banner
                uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
                for (int y = 0; y < 540; ++y) {
                    int screenY = heroY + y;
                    if (screenY < 0 || screenY >= 1080) continue;
                    float yAlpha = (y > 350) ? std::min(1.0f, static_cast<float>(y - 350) / 190.0f) : 0.0f;
                    for (int x = 0; x < 950; ++x) {
                        float xAlpha = (x < 700) ? 1.0f - static_cast<float>(x) / 700.0f : 0.0f;
                        float totalAlpha = std::min(1.0f, xAlpha * 0.95f + yAlpha * 0.95f);
                        if (totalAlpha > 0.01f) {
                            blendPixelSwizzled(base, x, screenY, 0xFF0E0B16, static_cast<uint8_t>(totalAlpha * 255.0f));
                        }
                    }
                    if (y > 400) {
                        float bAlpha = std::min(1.0f, static_cast<float>(y - 400) / 140.0f);
                        for (int x = 950; x < 1920; ++x) {
                            blendPixelSwizzled(base, x, screenY, 0xFF0E0B16, static_cast<uint8_t>(bAlpha * 255.0f));
                        }
                    }
                }

                // Lado izquierdo del Hero (x = 68):
                bool btn0Focused = (focusZone == 2) ? (heroBtnIdx == 0) : heroFocused;
                bool btn1Focused = (focusZone == 2 && heroBtnIdx == 1);

                // Badge de GameNight si es pestaña GameNight
                if (activeTabIdx == 1) {
                    drawRoundedRect(68, heroY + 80, 290, 32, 8, 0xFF7C3AED);
                    drawText(82, heroY + 88, 1, "[GN] USA TELEFONOS COMO MANDOS", 0xFFFFFFFF);
                }

                if (isFirstSlide) {
                    drawText(68, heroY + 140, 6, "Luna Premium", 0xFFFFFFFF);
                    drawText(68, heroY + 310, 2, "Mas de 100 juegos. Una suscripcion.", 0xFFE2E8F0);
                    drawPillButton(68, heroY + 365, 280, 58, "Mas informacion", btn0Focused, "", false);
                } else {
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
                    std::string sub = heroGame.subtitle.empty() ? (heroGame.synopsis.empty() ? "Descubre grandes titulos para jugar en la nube." : heroGame.synopsis) : heroGame.subtitle;
                    drawText(68, heroY + 310, 2, utf8SafeTruncate(sub, 54), 0xFFE2E8F0);
                    drawPillButton(68, heroY + 365, 280, 58, "Jugar ahora", btn0Focused, "", true);
                    drawPillButton(364, heroY + 365, 280, 58, "Mas informacion", btn1Focused, "", false);
                }

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
        int baseRowY = (hasHero ? 515 : 180) - yOffset;

        for (size_t r = firstRowIdx; r < collections.size(); ++r) {
            int rowY = baseRowY + static_cast<int>(r - firstRowIdx) * 360;
            if (rowY + 360 < 0 || rowY > 1080) continue;

            const auto& row = collections[r];
            bool isFocusedRow = (focusZone != 0 && static_cast<int>(r) == clampedRow);
            int selectedCol = (r < rowSelectedCol.size()) ? rowSelectedCol[r] : 0;

            // CASO A: BANNER PROMOCIONAL DE PARTNERS (PARTNER_UPSELL_ROW)
            if (row.isPartnerRow) {
                int bannerW = 1784;
                int bannerH = 220;
                drawRoundedRect(68, rowY + 10, bannerW, bannerH, 16, 0xFF241B3B);
                drawRoundedBorder(68, rowY + 10, bannerW, bannerH, 16, isFocusedRow ? 3 : 1, isFocusedRow ? 0xFFFFFFFF : 0xFF4C1D95);

                drawText(100, rowY + 40, 4, row.partnerTitle, 0xFFFFFFFF);
                drawText(100, rowY + 90, 2, row.partnerDescription, 0xFFCBD5E1);
                drawPillButton(100, rowY + 140, 280, 54, row.partnerButtonText, isFocusedRow, "", false);

                // Arte a la derecha
                if (!row.partnerPromoImage.empty()) {
                    auto bImg = luna::util::ImageLoader::instance().getImage(row.partnerPromoImage, luna::util::ImagePreset::PanoramicHero);
                    if (bImg && bImg->ready) {
                        drawPreScaledImage(1300, rowY + 20, bImg->rgba.data(), 520, 200, bImg->width, bImg->height);
                    } else {
                        luna::util::ImageLoader::instance().requestImage(row.partnerPromoImage, luna::util::ImagePreset::PanoramicHero);
                    }
                }
                continue;
            }

            // CASO B: EXPLORAR CATEGORIAS (CATEGORY_CAROUSEL)
            if (row.isCategoryCarousel) {
                drawText(68, rowY + 10, 3, "Explorar categorias", 0xFFFFFFFF);
                int sqSize = 180;
                int sqGap = 24;
                int scrollOff = std::max(0, (selectedCol - 4) * (sqSize + sqGap));
                for (size_t c = 0; c < row.items.size(); ++c) {
                    int cx = 68 + static_cast<int>(c) * (sqSize + sqGap) - scrollOff;
                    if (cx + sqSize < 0 || cx > 1920) continue;
                    bool isSel = isFocusedRow && (static_cast<int>(c) == selectedCol);

                    drawRoundedRect(cx, rowY + 50, sqSize, sqSize, 14, isSel ? 0xFF9146FF : 0xFF1C152E);
                    drawRoundedBorder(cx, rowY + 50, sqSize, sqSize, 14, isSel ? 3 : 1, isSel ? 0xFFFFFFFF : 0xFF2D2248);

                    drawText(cx + 20, rowY + 120, 3, utf8SafeTruncate(row.items[c].title, 12), 0xFFFFFFFF);
                    drawText(cx + 20, rowY + 155, 1, "Ver juegos", isSel ? 0xFFFCD34D : 0xFF94A3B8);
                }
                continue;
            }

            // CASO C: JUGADOS RECIENTEMENTE (16:9 EN MI ESPACIO)
            if (row.isRecentlyPlayed16_9 || (activeTabIdx == 2 && r == 0)) {
                drawText(68, rowY + 10, 3, row.title.empty() ? "Jugados recientemente" : row.title, 0xFFFFFFFF);
                int c16W = 290;
                int c16H = 164;
                int c16Gap = 24;
                int scrollOff = std::max(0, (selectedCol - 3) * (c16W + c16Gap));

                for (size_t i = 0; i < row.items.size(); ++i) {
                    int cardX = 68 + static_cast<int>(i) * (c16W + c16Gap) - scrollOff;
                    if (cardX + c16W < 0 || cardX > 1920) continue;
                    bool isSel = isFocusedRow && (static_cast<int>(i) == selectedCol);
                    int cardY = rowY + 50;

                    const auto& item = row.items[i];
                    std::string imgUrl = item.backdropUrl.empty() ? item.coverUrl : item.backdropUrl;
                    auto cImg = luna::util::ImageLoader::instance().getImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
                    if (cImg && cImg->ready) {
                        drawPreScaledImage(cardX, cardY, cImg->rgba.data(), c16W, c16H, cImg->width, cImg->height);
                    } else {
                        if (!imgUrl.empty()) {
                            luna::util::ImageLoader::instance().requestImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
                        }
                        drawRoundedRect(cardX, cardY, c16W, c16H, 12, 0xFF241B3B);
                    }

                    if (isSel) {
                        drawRoundedBorder(cardX - 2, cardY - 2, c16W + 4, c16H + 4, 14, 3, 0xFFFFFFFF);
                    }

                    // Titulo y Fecha relativa/exacta (28 oct, 27 oct)
                    drawText(cardX, cardY + 175, 2, utf8SafeTruncate(item.title, 22), 0xFFFFFFFF);
                    std::string dateStr = item.lastPlayedDate.empty() ? "Partida reciente" : ("Ultima partida: " + item.lastPlayedDate);
                    drawText(cardX, cardY + 202, 1, dateStr, 0xFF10B981);
                }
                continue;
            }

            // CASO D: SWIMLANE ESTANDAR CON POSTERS VERTICALES (Y RANKINGS 1..6)
            drawText(68, rowY + 10, 3, row.title, 0xFFFFFFFF);

            int cardW = 200;
            int cardH = 280;
            int curCardGap = 28;
            int rankMargin = row.isRankings ? 72 : 0;
            int slotWidth = cardW + curCardGap + rankMargin;
            int scrollOffset = std::max(0, (selectedCol - 3) * slotWidth);

            int rankNumberIndex = 1;
            for (size_t i = 0; i < row.items.size(); ++i) {
                const auto& item = row.items[i];
                if (row.isRankings) {
                    if (item.title.find("Luna Premium") != std::string::npos ||
                        item.id.find("luna_premium") != std::string::npos) {
                        continue;
                    }
                }

                int slotX = 68 + static_cast<int>(rankNumberIndex - 1) * slotWidth - scrollOffset;
                int numberX = slotX;
                int cardX = slotX + rankMargin;
                if (cardX + cardW < 0 || numberX > 1920) {
                    rankNumberIndex++;
                    continue;
                }

                bool isSelectedCard = isFocusedRow && (static_cast<int>(i) == selectedCol);
                int cardY = rowY + 50;

                if (row.isRankings) {
                    std::string rankStr = std::to_string(rankNumberIndex);
                    drawText(numberX, cardY + cardH - 95, 8, rankStr, 0x35FFFFFF);
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
                rankNumberIndex++;
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

    // 6. POPUP MENU PERFIL DE USUARIO (10 OPCIONES OFICIALES)
    if (profileMenuOpen) {
        int pw = 480, ph = 640;
        int px = 1920 - pw - 40;
        int py = 76;
        drawRoundedRect(px, py, pw, ph, 16, 0xF8140F24);
        drawRoundedBorder(px, py, pw, ph, 16, 2, 0xFF9146FF);

        drawRoundedRect(px + 24, py + 20, 48, 48, 24, 0xFF14B8A6);
        drawText(px + 40, py + 30, 3, "B", 0xFFFFFFFF);
        drawText(px + 86, py + 22, 3, profile.displayName + " " + profile.discriminator, 0xFFFFFFFF);
        drawText(px + 86, py + 50, 1, profile.planStatus, 0xFF10B981);

        fillRect(px + 24, py + 80, pw - 48, 1, 0xFF2D2545);

        static const char* kOfficialProfileOpts[10] = {
            "Quien va a jugar en Luna? (Perfiles)",
            "Planes de Luna (Luna Premium 9,99 EUR)",
            "Mis distintivos y logros",
            "Transmision de Twitch",
            "Cuentas vinculadas (Ubisoft, EA, Epic)",
            "Ajustes de la aplicacion",
            "Novedades de Luna",
            "Ayuda y asistencia tecnica",
            "Acerca de Luna y legal (v1.8.197368.0)",
            "Cerrar sesion de Amazon"
        };

        for (int m = 0; m < 10; ++m) {
            int by = py + 92 + m * 52;
            bool btnFoc = (profileMenuIdx == m);
            if (btnFoc) {
                drawRoundedRect(px + 16, by, pw - 32, 44, 8, 0xFF9146FF);
                drawText(px + 32, by + 12, 2, kOfficialProfileOpts[m], 0xFFFFFFFF);
            } else {
                drawText(px + 32, by + 12, 2, kOfficialProfileOpts[m], 0xFFCBD5E1);
            }
        }
    }

    // 7. VISTA DE BUSQUEDA EN CUADRICULA DE 5 COLUMNAS EN 16:9
    if (searchOpen) {
        fillRect(0, 0, 1920, 1080, 0xF80B0912);
        drawRoundedRect(68, 28, 1784, 54, 10, 0xFF1C152E);
        drawRoundedBorder(68, 28, 1784, 54, 10, 2, 0xFF9146FF);
        drawText(90, 44, 2, "BUSQUEDA Y EXPLORACION DE JUEGOS EN AMAZON LUNA", 0xFFFFFFFF);
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

        // Cuadricula de 5 columnas en 16:9 (290x164 px)
        int gBaseX = 68;
        int gBaseY = 150;
        int gCardW = 290;
        int gCardH = 164;
        int gGapX = 35;
        int gGapY = 70;
        int numFiltered = static_cast<int>(filteredGames.size());
        for (int g = 0; g < numFiltered && g < 15; ++g) {
            int col = g % 5;
            int row = g / 5;
            int gx = gBaseX + col * (gCardW + gGapX);
            int gy = gBaseY + row * (gCardH + gGapY);
            bool gFoc = (searchGridIdx == g);

            const auto& tile = filteredGames[g];
            std::string imgUrl = tile.backdropUrl.empty() ? tile.coverUrl : tile.backdropUrl;
            auto cImg = luna::util::ImageLoader::instance().getImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
            if (cImg && cImg->ready) {
                drawPreScaledImage(gx, gy, cImg->rgba.data(), gCardW, gCardH, cImg->width, cImg->height);
            } else {
                if (!imgUrl.empty()) {
                    luna::util::ImageLoader::instance().requestImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
                }
                drawRoundedRect(gx, gy, gCardW, gCardH, 12, 0xFF241B3B);
            }
            if (gFoc) {
                drawRoundedBorder(gx - 2, gy - 2, gCardW + 4, gCardH + 4, 14, 3, 0xFFFFFFFF);
            }
            drawText(gx, gy + 172, 2, utf8SafeTruncate(tile.title, 20), 0xFFFFFFFF);
            drawText(gx, gy + 196, 1, utf8SafeTruncate(tile.genres, 24), 0xFF94A3B8);
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
                                          uint64_t frameCount,
                                          bool isFavorite) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    // 1. Fondo principal oficial
    clear(0xFF0E0B16);

    // 2. Fondo panoramico 16:9 completo con degradado cinematografico 1:1
    auto bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
    if (!bgImg || !bgImg->ready) {
        bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicHero);
    }
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

    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    for (int y = 0; y < 1080; ++y) {
        float yAlpha = (y > 480) ? std::min(1.0f, static_cast<float>(y - 480) / 380.0f) : 0.0f;
        for (int x = 0; x < 1050; ++x) {
            float xAlpha = (x < 850) ? 1.0f - static_cast<float>(x) / 850.0f : 0.0f;
            float totalAlpha = std::min(1.0f, xAlpha * 0.95f + yAlpha * 0.95f);
            if (totalAlpha > 0.01f) {
                blendPixelSwizzled(base, x, y, 0xFF0E0B16, static_cast<uint8_t>(totalAlpha * 255.0f));
            }
        }
        if (y > 520) {
            float bAlpha = std::min(0.92f, static_cast<float>(y - 520) / 340.0f);
            for (int x = 1050; x < 1920; ++x) {
                blendPixelSwizzled(base, x, y, 0xFF0E0B16, static_cast<uint8_t>(bAlpha * 255.0f));
            }
        }
    }

    // Barra superior: < Volver
    fillRect(0, 0, 1920, 68, 0x990E0B16);
    drawText(44, 22, 2, "<  Volver al catalogo", 0xFFE2E8F0);
    int magX = 1754, magY = 32, magR = 8;
    drawRoundedBorder(magX - magR, magY - magR, magR * 2, magR * 2, magR, 2, 0xFFCBD5E1);
    for (int d = 0; d < 6; ++d) {
        fillRect(magX + magR - 3 + d, magY + magR - 3 + d, 2, 2, 0xFFCBD5E1);
    }
    drawRoundedRect(1810, 16, 38, 38, 19, 0xFF14B8A6);
    drawText(1822, 24, 2, "B", 0xFFFFFFFF);

    // 3. COLUMNA IZQUIERDA (x = 68): Ficha y Sinopsis
    bool drawnLogo = false;
    if (!game.logoUrl.empty()) {
        auto logoImg = luna::util::ImageLoader::instance().getImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
        if (logoImg && logoImg->ready) {
            drawPreScaledImage(68, 120, logoImg->rgba.data(), logoImg->width, logoImg->height, logoImg->width, logoImg->height);
            drawnLogo = true;
        } else {
            luna::util::ImageLoader::instance().requestImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
        }
    }
    if (!drawnLogo) {
        drawText(68, 140, 6, utf8SafeTruncate(game.title, 26), 0xFFFFFFFF);
    }

    std::string genreText = game.genres.empty() ? "Accion, Aventura" : game.genres;
    drawText(68, 290, 2, genreText, 0xFFA0A0B0);

    // Botones de accion oficiales:
    // [ ▶ Jugar ahora ]  [ ★ ]  [ 👥 Remote Play ]
    int btnH = 60;
    bool primFocused = (detailZone == 0 && detailBtnIdx == 0);
    bool favFocused = (detailZone == 0 && detailBtnIdx == 1);
    bool rpFocused = (detailZone == 0 && detailBtnIdx == 2);

    if (game.isEntitled) {
        std::string label = isConnecting ? "CONECTANDO..." : "Jugar ahora";
        drawPillButton(68, 330, 260, btnH, label, primFocused, "", true);
    } else {
        std::string pri = game.buttonText.empty() ? "Incluido en Luna Premium" : game.buttonText;
        drawPillButton(68, 330, 260, btnH, pri, primFocused, "9,99 EUR/mes");
    }

    // Boton de Favoritos con estrella
    drawRoundedRect(340, 330, 60, btnH, 14, favFocused ? 0xFFFFFFFF : 0xFF2D2248);
    uint32_t favStarColor = isFavorite ? 0xFFF59E0B : (favFocused ? 0xFF0E0B16 : 0xFFFFFFFF);
    drawStarIcon(370, 360, 12, favStarColor);

    // Boton Remote Play
    drawRoundedRect(412, 330, 200, btnH, 14, rpFocused ? 0xFF9146FF : 0xFF2D2248);
    drawRoundedBorder(412, 330, 200, btnH, 14, rpFocused ? 2 : 1, rpFocused ? 0xFFFFFFFF : 0xFF4C1D95);
    drawText(432, 350, 2, "Remote Play", 0xFFFFFFFF);

    // Sinopsis multilineas oficial
    auto synLines = utf8WrapLines(game.synopsis, 75, 4);
    int synY = 415;
    for (const auto& l : synLines) {
        drawText(68, synY, 2, l, 0xFFCBD5E1);
        synY += 24;
    }

    // Galeria de Trailers y Capturas
    size_t numScreens = game.screenshotUrls.size();
    if (numScreens > 0) {
        drawText(68, 545, 3, "Trailer y capturas de pantalla", 0xFFFFFFFF);
        int thumbW = 250, thumbH = 140, thumbGap = 16;
        int thumbY = 585;
        for (size_t t = 0; t < numScreens && t < 3; ++t) {
            int tx = 68 + static_cast<int>(t) * (thumbW + thumbGap);
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
                drawRoundedRect(tx + (thumbW - 48) / 2, thumbY + (thumbH - 48) / 2, 48, 48, 24, 0xCC0E0B16);
                drawPlayTriangleIcon(tx + thumbW / 2, thumbY + thumbH / 2, 10, 0xFFFFFFFF);
            }
            if (detailZone == 1 && similarSelectedIdx == static_cast<int>(t)) {
                drawRoundedBorder(tx - 2, thumbY - 2, thumbW + 4, thumbH + 4, 12, 3, 0xFFFFFFFF);
            }
        }
    }

    // FAQs oficiales
    int faqY = 750;
    drawText(68, faqY, 3, "Preguntas frecuentes", 0xFFFFFFFF);
    static const char* kFaqTitles[] = {
        "[?] Que es Amazon Luna y como funciona?",
        "[?] Como invitar a amigos a jugar con Remote Play?",
        "[?] Que mandos y dispositivos son compatibles?"
    };
    for (int q = 0; q < 3; ++q) {
        int qy = faqY + 36 + q * 42;
        bool qFoc = (detailZone == 3 && similarSelectedIdx == q);
        drawRoundedRect(68, qy, 800, 36, 8, qFoc ? 0xFF9146FF : 0xFF1C152E);
        drawText(82, qy + 10, 2, kFaqTitles[q], qFoc ? 0xFFFFFFFF : 0xFFCBD5E1);
    }

    // 4. COLUMNA DERECHA (x = 1100): Tabla tecnica oficial de especificaciones
    int tabX = 1120;
    int tabY = 135;
    int tabW = 730;
    int tabH = 650;
    drawRoundedRect(tabX, tabY, tabW, tabH, 16, 0xEE161024);
    drawRoundedBorder(tabX, tabY, tabW, tabH, 16, 1, 0xFF2D2248);

    drawText(tabX + 32, tabY + 28, 4, "Especificaciones tecnicas", 0xFFFFFFFF);
    fillRect(tabX + 32, tabY + 68, tabW - 64, 1, 0xFF352654);

    struct SpecRow { const char* label; std::string value; };
    std::vector<SpecRow> specs = {
        {"Fecha de lanzamiento", game.releaseYear.empty() ? "2025" : game.releaseYear},
        {"Editor", utf8SafeTruncate(game.publisher.empty() ? "Amazon Luna" : game.publisher, 30)},
        {"Desarrollador", utf8SafeTruncate(game.developer.empty() ? "Amazon Games" : game.developer, 30)},
        {"Modos de juego", game.playerCount.empty() ? "1 jugador" : game.playerCount},
        {"Calidad de video", "Hasta 1080p a 60 fps (Full HD)"},
        {"Mandos compatibles", "Mando DualSense PS5, Mando Luna, Teclado"},
        {"Idiomas disponibles", "Espanol (Espana), Ingles, Frances, Aleman"},
        {"Calificacion por edades", game.pegiRating.empty() ? "PEGI 12" : game.pegiRating}
    };

    int sY = tabY + 84;
    for (const auto& sp : specs) {
        drawText(tabX + 32, sY, 2, sp.label, 0xFF94A3B8);
        drawText(tabX + 320, sY, 2, sp.value, 0xFFFFFFFF);
        sY += 46;
    }

    // PEGI rating visual dentro de la tabla
    int pegiBoxY = sY + 10;
    uint32_t pegiBg = 0xFFF59E0B;
    std::string pegiNum = "12";
    if (game.pegiRating.find("18") != std::string::npos) { pegiBg = 0xFFDC2626; pegiNum = "18"; }
    else if (game.pegiRating.find("16") != std::string::npos) { pegiBg = 0xFFEA580C; pegiNum = "16"; }
    else if (game.pegiRating.find("7") != std::string::npos || game.pegiRating.find("3") != std::string::npos) { pegiBg = 0xFF10B981; pegiNum = "7"; }

    drawRoundedRect(tabX + 32, pegiBoxY, 60, 68, 8, pegiBg);
    drawText(tabX + 46, pegiBoxY + 8, 1, "PEGI", 0xFF0E0B16);
    drawText(tabX + 42, pegiBoxY + 28, 3, pegiNum, 0xFF0E0B16);
    drawText(tabX + 108, pegiBoxY + 24, 2, "Apto segun normativa PEGI europea", 0xFFCBD5E1);

    // 5. Carrusel inferior "Tambien te puede interesar"
    if (!game.similarGames.empty()) {
        int simY = 890;
        drawText(68, simY, 3, "Tambien te puede interesar", 0xFFFFFFFF);
        int simCardW = 120, simCardH = 168, simGap = 20;
        int simCardsY = simY + 34;
        for (size_t s = 0; s < game.similarGames.size() && s < 10; ++s) {
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
                drawRoundedRect(sx, simCardsY, simCardW, simCardH, 8, 0xFF241B3B);
            }
            if (detailZone == 2 && similarSelectedIdx == static_cast<int>(s)) {
                drawRoundedBorder(sx - 2, simCardsY - 2, simCardW + 4, simCardH + 4, 10, 3, 0xFFFFFFFF);
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


void DisplayManager::drawSettingsScreen(int selectedItemIdx,
                                        const std::string& statusText,
                                        uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    clear(0xFF0E0B16);

    // Barra superior
    fillRect(0, 0, 1920, 90, 0xFF181226);
    fillRect(0, 90, 1920, 2, 0xFF9146FF);
    drawLunaLogoIcon(50, 24, 42);
    drawText(105, 30, 4, "Ajustes y Configuracion de Amazon Luna", 0xFFFFFFFF);
    drawText(1580, 36, 2, "[(O) Volver al inicio]", 0xFFCBD5E1);

    auto& s = luna::util::AppSettings::instance();

    // 10 Secciones oficiales de Ajustes
    struct SettingSection {
        const char* title;
        const char* desc;
        const char* icon;
    };
    static const SettingSection kSections[10] = {
        {"Mandos y Gamepads", "DualSense PS5, Mando Luna, Bluetooth", "[M]"},
        {"Cuentas vinculadas", "Ubisoft, EA, Epic Games, GOG, Twitch", "[C]"},
        {"Tu cuenta", "Jesus (Bornicho #8663) - Luna Estandar", "[U]"},
        {"Control parental", "Bloqueo por edades PEGI y PIN de 5 digitos", "[P]"},
        {"Calidad de audio y video", "1080p 60 FPS (Recomendado) o 720p", "[Q]"},
        {"Idioma y region", "13 idiomas disponibles (Espanol, English...)", "[L]"},
        {"Datos de partidas guardadas", "Sincronizacion en nube Amazon Cloud Save", "[S]"},
        {"Descargar Luna", "Disponible en PC, Mac, Fire TV, iOS y Android", "[D]"},
        {"Ayuda y sugerencias", "Diagnostico de conexion y soporte tecnico", "[H]"},
        {"Acerca de Luna y legal", "Version de cliente v1.8.197368.0", "[A]"}
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
        // Cuentas vinculadas
        drawText(pX + 40, dY, 3, "Proveedores y tiendas de juegos vinculadas:", 0xFFFFFFFF);
        static const char* kAccounts[] = {
            "Ubisoft Connect (Vinculada - Cuenta activa)",
            "Epic Games Store (Vinculada - Cuenta activa)",
            "Electronic Arts / EA Play (Vinculada)",
            "GOG Galaxy (Vinculada)",
            "Twitch (bornicho - Vinculada)"
        };
        for (int a = 0; a < 5; ++a) {
            int ay = dY + 45 + a * 65;
            drawRoundedRect(pX + 40, ay, pW - 80, 52, 10, 0xFF1E1633);
            drawText(pX + 60, ay + 16, 2, kAccounts[a], 0xFFFFFFFF);
            drawText(pX + pW - 200, ay + 16, 2, "[DESVINCULAR]", 0xFF94A3B8);
        }
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
        // Idioma y region
        drawText(pX + 40, dY, 3, "Idioma de la aplicacion y catalogo:", 0xFFFFFFFF);
        static const char* kLangs[] = {
            "Espanol (Espana) [ACTIVO]", "English (United States)", "English (United Kingdom)",
            "Deutsch (Deutschland)", "Francais (France)", "Italiano (Italia)",
            "Polski", "Nederlands", "Portugues (Brasil)", "Espanol (Latinoamerica)"
        };
        for (int l = 0; l < 8; ++l) {
            int ly = dY + 45 + l * 54;
            drawRoundedRect(pX + 40, ly, pW - 80, 44, 8, l == 0 ? 0xFF7C3AED : 0xFF1E1633);
            drawText(pX + 60, ly + 12, 2, kLangs[l], 0xFFFFFFFF);
        }
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
        img = luna::util::ImageLoader::instance().getImage(currentUrl, luna::util::ImagePreset::ScreenshotThumbnail);
    }

    if (img && img->ready) {
        drawPreScaledImage(0, 0, img->rgba.data(), 1920, 1080, img->width, img->height);
    } else {
        luna::util::ImageLoader::instance().requestImage(currentUrl, luna::util::ImagePreset::PanoramicDetail);
        luna::util::ImageLoader::instance().requestImage(currentUrl, luna::util::ImagePreset::ScreenshotThumbnail);
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
    if (!isReady_ || !mappedMemory_) return;

    clear(0xFF07050A);

    // Fondo oscurecido con imagen del juego
    auto bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
    if (!bgImg || !bgImg->ready) {
        bgImg = luna::util::ImageLoader::instance().getImage(game.coverUrl, luna::util::ImagePreset::PanoramicDetail);
    }
    if (bgImg && bgImg->ready) {
        drawPreScaledImage(0, 0, bgImg->rgba.data(), 1920, 1080, bgImg->width, bgImg->height);
    }

    // Vinetado oscuro radial sobre el fondo
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    for (int y = 0; y < 1080; ++y) {
        for (int x = 0; x < 1920; ++x) {
            blendPixelSwizzled(base, x, y, 0xFF07050A, 210);
        }
    }

    // PEGI rating arriba a la izquierda
    drawRoundedRect(68, 50, 60, 68, 8, 0xFFF59E0B);
    drawText(82, 58, 1, "PEGI", 0xFF0E0B16);
    drawText(78, 78, 3, "12", 0xFF0E0B16);

    // Boton [(X) Cancelar] arriba a la derecha
    drawRoundedRect(1650, 50, 200, 50, 10, 0xFF241B3B);
    drawRoundedBorder(1650, 50, 200, 50, 10, 1, 0xFF4C1D95);
    drawText(1680, 64, 2, "[(X) Cancelar]", 0xFFE2E8F0);

    // Logotipo o Titulo grande centrado
    bool drawnLogo = false;
    if (!game.logoUrl.empty()) {
        auto logoImg = luna::util::ImageLoader::instance().getImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
        if (logoImg && logoImg->ready) {
            int lx = (1920 - logoImg->width) / 2;
            int ly = 380;
            drawPreScaledImage(lx, ly, logoImg->rgba.data(), logoImg->width, logoImg->height, logoImg->width, logoImg->height);
            drawnLogo = true;
        }
    }
    if (!drawnLogo) {
        int tw = fontRenderer_.measureText(game.title, 48);
        drawText((1920 - tw) / 2, 430, 6, game.title, 0xFFFFFFFF);
    }

    // Spinner oficial de 3 puntos orbitando abajo a la derecha
    int spinX = 1480;
    int spinY = 960;
    int phase = static_cast<int>((frameCount / 6) % 3);
    for (int p = 0; p < 3; ++p) {
        uint32_t dotColor = (p == phase) ? 0xFFFFFFFF : 0x6694A3B8;
        int dotR = (p == phase) ? 7 : 5;
        drawRoundedRect(spinX + p * 24, spinY - dotR, dotR * 2, dotR * 2, dotR, dotColor);
    }
    std::string msg = statusMsg.empty() ? "Iniciando tu sesion de juego en la nube..." : statusMsg;
    drawText(spinX - 440, spinY - 6, 2, msg, 0xFFCBD5E1);

    // Modal de confirmacion de cancelacion si esta abierto
    if (cancelModalOpen) {
        fillRect(0, 0, 1920, 1080, 0xCC000000);
        int mw = 840, mh = 320;
        int mx = (1920 - mw) / 2, my = (1080 - mh) / 2;
        drawRoundedRect(mx, my, mw, mh, 16, 0xFF181226);
        drawRoundedBorder(mx, my, mw, mh, 16, 2, 0xFF9146FF);

        drawText(mx + 50, my + 45, 4, "Salir del juego y abandonar la cola?", 0xFFFFFFFF);
        drawText(mx + 50, my + 105, 2, "Perderas tu posicion actual en la cola de servidores de Amazon Luna.", 0xFFCBD5E1);

        bool btn0Foc = (cancelModalBtnIdx == 0);
        bool btn1Foc = (cancelModalBtnIdx == 1);
        drawPillButton(mx + 50, my + 180, 340, 60, "Continuar en la cola", btn0Foc, "", false);
        drawPillButton(mx + 420, my + 180, 340, 60, "Salir del juego", btn1Foc, "", false);
    }

    present();
}

void DisplayManager::drawPostSessionModal(const std::vector<GameTile>& suggestions,
                                         int selectedSuggestionIdx,
                                         uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    fillRect(0, 0, 1920, 1080, 0xDD07050A);

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

    fillRect(0, 0, 1920, 1080, 0xD0000000);

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

    fillRect(0, 0, 1920, 1080, 0xD0000000);

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

#if defined(LUNA_TARGET_PS5)
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    flushCacheRange(base, kUsedBytes);
    flipMarker_++;
    sceVideoOutSubmitFlip(videoHandle_, currentBuffer_, 1, flipMarker_);
#endif

    currentBuffer_ = (currentBuffer_ + 1) % kBuffers;
}

} // namespace luna::display
