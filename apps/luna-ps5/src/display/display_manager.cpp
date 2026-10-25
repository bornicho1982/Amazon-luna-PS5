#include "display/display_manager.h"
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

void drawSwimlaneRow(DisplayManager& disp,
                     const LunaCollection& row,
                     int rowY,
                     bool isFocusedRow,
                     int selectedCol,
                     int rowIndex,
                     int totalRows) {
    // Titulo de la fila oficial de Amazon Luna
    uint32_t titleCol = isFocusedRow ? 0xFFFFFFFF : 0xFFC8BFE7;
    disp.drawText(60, rowY, 3, row.title, titleCol);

    if (!row.subtitle.empty()) {
        std::string sub = utf8SafeTruncate(row.subtitle, 72);
        disp.drawText(60 + (static_cast<int>(row.title.size()) * 14) + 25, rowY + 6, 1, sub, 0xFF9F8FEF);
    }

    int itemCount = static_cast<int>(row.items.size());
    if (itemCount <= 0) return;
    if (selectedCol < 0) selectedCol = 0;
    if (selectedCol >= itemCount) selectedCol = itemCount - 1;

    std::string countTag = "Elemento " + std::to_string(selectedCol + 1) + " de " + std::to_string(itemCount) +
                           "   •   Fila " + std::to_string(rowIndex) + "/" + std::to_string(totalRows);
    disp.drawText(1520, rowY + 6, 1, countTag, isFocusedRow ? 0xFF02FDDB : 0xFF6E628A);

    int cardY = rowY + 38;
    int cardW = 336;
    int cardH = 352;
    int cardGap = 28;
    int visibleCount = 5;

    int scrollOffset = 0;
    if (selectedCol >= visibleCount) {
        scrollOffset = selectedCol - visibleCount + 1;
    }
    if (scrollOffset + visibleCount > itemCount) {
        scrollOffset = itemCount - visibleCount;
    }
    if (scrollOffset < 0) scrollOffset = 0;

    int totalVisible = std::min(visibleCount, itemCount - scrollOffset);

    for (int vi = 0; vi < totalVisible; ++vi) {
        int i = scrollOffset + vi;
        int cardX = 60 + vi * (cardW + cardGap);
        bool isSelected = isFocusedRow && (i == selectedCol);
        const auto& item = row.items[i];

        uint32_t cardBg = isSelected ? 0xFF261E3A : 0xFF171226;
        uint32_t cardBorder = isSelected ? 0xFFFFFFFF : 0xFF2B2144;
        int borderW = isSelected ? 3 : 1;

        drawCard(disp, cardX, cardY, cardW, cardH, cardBg, cardBorder, borderW);
        if (isSelected) {
            drawCard(disp, cardX + 3, cardY + 3, cardW - 6, cardH - 6, 0x00000000, 0xFF9146FF, 2);
        }

        // Barra superior de la tarjeta: Ranking (#1..#10) o Canal + PEGI
        disp.fillRect(cardX + borderW, cardY + borderW, cardW - (borderW * 2), 28,
                      isSelected ? 0xFF36245E : 0xFF1D1630);

        if (row.isRankings) {
            std::string rankStr = "#" + std::to_string(i + 1) + " TOP LUNA";
            disp.fillRect(cardX + 12, cardY + 6, 18, 18, 0xFF02FDDB);
            disp.drawText(cardX + 15, cardY + 7, 1, std::to_string(i + 1), 0xFF0E0B16);
            disp.drawText(cardX + 36, cardY + 7, 1, rankStr, 0xFF02FDDB);
        } else {
            disp.fillRect(cardX + 12, cardY + 10, 8, 8, item.badgeColor);
            disp.drawText(cardX + 26, cardY + 7, 1, item.category, isSelected ? 0xFF02FDDB : 0xFFB8B2C8);
        }
        disp.drawText(cardX + cardW - 76, cardY + 7, 1, item.pegiRating, 0xFFFDE68A);

        // Marco de portada
        int artX = cardX + 12;
        int artY = cardY + 34;
        int artW = cardW - 24;
        int artH = 198;

        drawCard(disp, artX, artY, artW, artH, 0xFF0E0B16, isSelected ? 0xFF9146FF : 0xFF241B3B, 1);

        if (!item.coverRgba.empty() && item.coverWidth > 0 && item.coverHeight > 0) {
            disp.drawImage(artX + 1, artY + 1, artW - 2, artH - 2,
                           item.coverRgba.data(), item.coverWidth, item.coverHeight);
        } else {
            disp.fillRect(artX + 1, artY + 1, artW - 2, artH - 2, 0xFF151026);
            disp.fillRect(artX + 12, artY + 12, artW - 24, 6, item.badgeColor);
            disp.drawText(artX + 16, artY + 55, 3, utf8SafeTruncate(item.title, 22), 0xFFFFFFFF);
            disp.drawText(artX + 16, artY + 105, 2, utf8SafeTruncate(item.publisher, 26), 0xFFB8B2C8);
            disp.drawText(artX + 16, artY + 145, 1, item.genres, 0xFF02FDDB);
        }

        // Metadatos reales bajo la portada
        disp.drawText(cardX + 14, cardY + 240, 2, utf8SafeTruncate(item.title, 25), 0xFFFFFFFF);

        std::string pubYear = item.publisher;
        if (!item.releaseYear.empty()) pubYear += " • " + item.releaseYear;
        disp.drawText(cardX + 14, cardY + 268, 1, utf8SafeTruncate(pubYear, 34), 0xFFB8B2C8);

        disp.drawText(cardX + 14, cardY + 290, 1, utf8SafeTruncate(item.genres, 34), 0xFFC8A2FF);

        // Barra de accion inferior de la tarjeta
        if (isSelected) {
            disp.fillRect(cardX + 12, cardY + 312, cardW - 24, 30, 0xFF9146FF);
            disp.drawText(cardX + 22, cardY + 318, 1, "(X) Ver detalles   |   [CUADRADO] Jugar ya", 0xFFFFFFFF);
        } else {
            disp.drawText(cardX + 14, cardY + 318, 1, "1080p 60 FPS • " + item.inputMethods, 0xFF6E628A);
        }
    }
}

} // namespace

void DisplayManager::drawLunaHomeScreen(const std::vector<LunaCollection>& collections,
                                        const std::vector<std::string>& navTabs,
                                        int activeTabIdx,
                                        int focusZone,
                                        int selectedRowIdx,
                                        const std::vector<int>& rowSelectedCol,
                                        int heroBtnIdx,
                                        const PlayerProfileInfo& profile,
                                        bool padConnected,
                                        const std::string& statusText,
                                        uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;

    // 1. Fondo principal oficial de Amazon Luna (#0E0B16 Cosmic Deep)
    clear(0xFF0E0B16);

    // 2. ZONA 0: Barra de Navegacion Superior Oficial (multistate_navigation / navigation_bar)
    fillRect(0, 0, 1920, 76, 0xFF130E21);
    fillRect(0, 76, 1920, 2, (focusZone == 0) ? 0xFF02FDDB : 0xFF281E42);

    // Logotipo oficial de Amazon Luna (▲ luna + JUEGOS EN LA NUBE)
    fillRect(52, 26, 16, 16, 0xFF9146FF);
    fillRect(56, 30, 8, 8, 0xFF02FDDB);
    drawText(76, 16, 5, "luna", 0xFFFFFFFF);
    drawText(195, 29, 1, "JUEGOS EN LA NUBE", 0xFFC8A2FF);

    // Pestañas principales: Inicio | GameNight | Biblioteca | Luna+ | Catálogo A-Z
    drawText(370, 26, 2, "[L2]", 0xFF9146FF);
    int tabX = 422;
    for (int t = 0; t < static_cast<int>(navTabs.size()); ++t) {
        bool isActive = (t == activeTabIdx);
        bool isNavFocused = (focusZone == 0 && isActive);
        int tabW = 28 + fontRenderer_.measureText(navTabs[t], 20);

        if (isNavFocused) {
            drawCard(*this, tabX, 16, tabW, 44, 0xFF9146FF, 0xFFFFFFFF, 3);
            drawText(tabX + 14, 26, 2, navTabs[t], 0xFFFFFFFF);
        } else if (isActive) {
            drawCard(*this, tabX, 16, tabW, 44, 0xFF281E42, 0xFF9146FF, 2);
            fillRect(tabX + 8, 57, tabW - 16, 3, 0xFF02FDDB);
            drawText(tabX + 14, 26, 2, navTabs[t], 0xFFFFFFFF);
        } else {
            drawText(tabX + 14, 26, 2, navTabs[t], 0xFFB8B2C8);
        }
        tabX += tabW + 12;
    }
    drawText(tabX + 4, 26, 2, "[R2]", 0xFF9146FF);

    // Derecha: Estado del mando DualSense + Badge oficial del jugador (Bornicho #8663 | Incluido con Prime)
    fillRect(1210, 32, 10, 10, padConnected ? 0xFF02FDDB : 0xFFEF4444);
    drawText(1226, 27, 1, padConnected ? "Mando conectado" : "Sin mando", padConnected ? 0xFF02FDDB : 0xFFFCA5A5);

    drawCard(*this, 1390, 14, 470, 48, 0xFF1C162B, 0xFF382B54, 1);
    fillRect(1402, 22, 32, 32, 0xFF9146FF);
    drawText(1412, 27, 2, profile.displayName.empty() ? "B" : profile.displayName.substr(0, 1), 0xFFFFFFFF);

    std::string playerHeader = profile.displayName + " " + profile.discriminator;
    drawText(1444, 19, 2, utf8SafeTruncate(playerHeader, 22), 0xFFFFFFFF);
    drawText(1444, 41, 1, "✓ " + profile.planStatus, 0xFF02FDDB);

    if (collections.empty()) {
        drawText(60, 200, 4, "Cargando colecciones de Amazon Luna...", 0xFFFFFFFF);
        present();
        return;
    }

    int clampedRow = std::max(0, std::min(selectedRowIdx, static_cast<int>(collections.size()) - 1));
    bool hasHero = collections[0].isHeroRotator && !collections[0].items.empty();

    // 3. ZONA 1 y ZONA 2: Vista Superior con Hero Rotator + Primera Fila, o Desplazamiento Vertical de Filas
    if (hasHero && clampedRow == 0) {
        const auto& heroCol = collections[0];
        int heroIdx = (!rowSelectedCol.empty()) ? rowSelectedCol[0] : 0;
        if (heroIdx < 0 || heroIdx >= static_cast<int>(heroCol.items.size())) heroIdx = 0;
        const auto& heroGame = heroCol.items[heroIdx];
        bool heroFocused = (focusZone != 0 && clampedRow == 0);

        int heroX = 60;
        int heroY = 88;
        int heroW = 1800;
        int heroH = 376;

        drawCard(*this, heroX, heroY, heroW, heroH, 0xFF181229,
                 heroFocused ? 0xFF9146FF : 0xFF2D224C, heroFocused ? 3 : 1);

        // Ilustracion cosmica ambiental de Luna en el lado derecho del Hero
        int planetCx = heroX + 1380;
        int planetCy = heroY + 188;
        for (int r = 150; r > 20; r -= 25) {
            uint32_t ringCol = (r > 110) ? 0xFF1F1638 : ((r > 70) ? 0xFF2D1E52 : 0xFF42267A);
            fillRect(planetCx - r, planetCy - (r / 2), r * 2, r, ringCol);
        }
        fillRect(planetCx - 200, planetCy - 4, 400, 8, 0xFF9146FF);
        fillRect(planetCx - 150, planetCy + 14, 300, 4, 0xFF02FDDB);

        // Badges superiores del Hero Rotator
        fillRect(heroX + 44, heroY + 30, 210, 28, 0xFF2D224C);
        fillRect(heroX + 44, heroY + 30, 6, 28, 0xFF02FDDB);
        drawText(heroX + 58, heroY + 35, 1, heroGame.category, 0xFF02FDDB);

        fillRect(heroX + 266, heroY + 30, 88, 28, 0xFF261E3A);
        drawText(heroX + 278, heroY + 35, 1, heroGame.pegiRating, 0xFFFDE68A);

        std::string pubMeta = heroGame.publisher + "  •  " + heroGame.releaseYear + "  •  " + heroGame.genres;
        drawText(heroX + 368, heroY + 35, 1, utf8SafeTruncate(pubMeta, 58), 0xFFC8A2FF);

        // Titulo del juego en tamaño gigante
        drawText(heroX + 44, heroY + 68, 6, utf8SafeTruncate(heroGame.title, 34), 0xFFFFFFFF);

        // Sinopsis real en español (hasta 2 líneas envueltas por palabra)
        auto synLines = utf8WrapLines(heroGame.synopsis, 82, 2);
        int synY = heroY + 144;
        for (const auto& ln : synLines) {
            drawText(heroX + 44, synY, 2, ln, 0xFFE2E8F0);
            synY += 28;
        }

        // Especificaciones de streaming
        drawText(heroX + 44, heroY + 208, 1,
                 "✓ Incluido con Prime   •   1080p 60 FPS Full HD   •   " + heroGame.inputMethods + "   •   " + heroGame.playerCount,
                 0xFF02FDDB);

        // Dos botones oficiales tipo pildora de Amazon Luna: [▶ Jugar ahora] y [ⓘ Más información]
        bool isStarting = (statusText.find("Asignando") != std::string::npos ||
                           statusText.find("Solicitando") != std::string::npos ||
                           statusText.find("Conectando") != std::string::npos);

        int btnPlayX = heroX + 44;
        int btnPlayY = heroY + 246;
        int btnPlayW = 340;
        int btnPlayH = 60;
        bool playFocused = heroFocused && (heroBtnIdx == 0);

        drawCard(*this, btnPlayX, btnPlayY, btnPlayW, btnPlayH,
                 isStarting ? 0xFF047857 : (playFocused ? 0xFF9146FF : 0xFF5B21B6),
                 playFocused ? 0xFFFFFFFF : 0xFF9146FF, playFocused ? 3 : 1);
        drawText(btnPlayX + 28, btnPlayY + 16, 3,
                 isStarting ? "CONECTANDO..." : "▶  Jugar ahora", 0xFFFFFFFF);

        int btnInfoX = btnPlayX + btnPlayW + 22;
        int btnInfoY = heroY + 246;
        int btnInfoW = 330;
        int btnInfoH = 60;
        bool infoFocused = heroFocused && (heroBtnIdx == 1);

        drawCard(*this, btnInfoX, btnInfoY, btnInfoW, btnInfoH,
                 infoFocused ? 0xFF3B2D5E : 0xFF261E3A,
                 infoFocused ? 0xFFFFFFFF : 0xFF4C3B78, infoFocused ? 3 : 1);
        drawText(btnInfoX + 28, btnInfoY + 16, 3, "Más información", 0xFFFFFFFF);

        // Puntos paginadores del rotador (• • •) y guia de cambio de diapositiva
        for (size_t d = 0; d < heroCol.items.size() && d < 8; ++d) {
            int dotX = heroX + 44 + static_cast<int>(d) * 24;
            int dotY = heroY + 332;
            if (static_cast<int>(d) == heroIdx) {
                fillRect(dotX, dotY, 18, 8, 0xFF02FDDB);
            } else {
                fillRect(dotX, dotY, 8, 8, 0xFF4C3B78);
            }
        }
        drawText(heroX + 250, heroY + 327, 1,
                 "[L1 / R1] Cambiar juego destacado (" + std::to_string(heroIdx + 1) + "/" +
                 std::to_string(heroCol.items.size()) + ")   |   [ABAJO] Explorar filas de juegos",
                 0xFFB8B2C8);

        // Portada destacada a la derecha del Hero
        if (!heroGame.coverRgba.empty() && heroGame.coverWidth > 0 && heroGame.coverHeight > 0) {
            int hcW = 240;
            int hcH = 330;
            int hcX = heroX + heroW - hcW - 50;
            int hcY = heroY + (heroH - hcH) / 2;
            drawCard(*this, hcX - 4, hcY - 4, hcW + 8, hcH + 8, 0xFF0E0B16, 0xFF9146FF, 2);
            drawImage(hcX, hcY, hcW, hcH, heroGame.coverRgba.data(), heroGame.coverWidth, heroGame.coverHeight);
        }

        // Renderizar la primera fila horizontal debajo del Hero Banner (p.ej. "Las mejores novedades en juegos")
        if (collections.size() > 1) {
            int r1Col = (rowSelectedCol.size() > 1) ? rowSelectedCol[1] : 0;
            drawSwimlaneRow(*this, collections[1], 482, false, r1Col, 1, static_cast<int>(collections.size()) - 1);
        }

        // Indicador de filas adicionales debajo
        if (collections.size() > 2) {
            fillRect(60, 885, 1800, 78, 0xFF130E21);
            fillRect(60, 885, 1800, 2, 0xFF281E42);
            drawText(84, 902, 2, "▼  Siguiente fila: " + collections[2].title + " (" +
                     std::to_string(collections[2].items.size()) + " juegos)", 0xFFC8A2FF);
            drawText(84, 932, 1, "Pulsa [ABAJO] en el D-Pad o Stick Izquierdo para desplazarte por las " +
                     std::to_string(collections.size() - 1) + " colecciones de Amazon Luna", 0xFF02FDDB);
        }
    } else {
        // Modo Swimlanes (2 filas horizontales completas visibles simultáneamente con scroll vertical suave)
        int firstSwimlaneIdx = hasHero ? 1 : 0;
        int topRow = clampedRow;
        if (topRow + 1 >= static_cast<int>(collections.size()) && topRow > firstSwimlaneIdx) {
            topRow = static_cast<int>(collections.size()) - 2;
        }
        if (topRow < firstSwimlaneIdx) topRow = firstSwimlaneIdx;

        int totalSwimlanes = static_cast<int>(collections.size()) - firstSwimlaneIdx;

        // Fila Superior en el viewport (y = 92)
        if (topRow < static_cast<int>(collections.size())) {
            int selColTop = (topRow < static_cast<int>(rowSelectedCol.size())) ? rowSelectedCol[topRow] : 0;
            bool isTopFocused = (focusZone != 0 && clampedRow == topRow);
            drawSwimlaneRow(*this, collections[topRow], 92, isTopFocused, selColTop,
                            topRow - firstSwimlaneIdx + 1, std::max(1, totalSwimlanes));
        }

        // Fila Inferior en el viewport (y = 530)
        if (topRow + 1 < static_cast<int>(collections.size())) {
            int botRow = topRow + 1;
            int selColBot = (botRow < static_cast<int>(rowSelectedCol.size())) ? rowSelectedCol[botRow] : 0;
            bool isBotFocused = (focusZone != 0 && clampedRow == botRow);
            drawSwimlaneRow(*this, collections[botRow], 530, isBotFocused, selColBot,
                            botRow - firstSwimlaneIdx + 1, std::max(1, totalSwimlanes));
        }
    }

    // 4. Barra inferior (Footer de estado y controles de navegacion 2D)
    int footerY = 978;
    int footerH = 88;
    drawCard(*this, 60, footerY, 1800, footerH, 0xFF130E21, 0xFF281E42, 1);

    bool isError = (statusText.find("Error") != std::string::npos ||
                    statusText.find("Fallo") != std::string::npos ||
                    statusText.find("denego") != std::string::npos);
    bool isBusy = (statusText.find("Asignando") != std::string::npos ||
                   statusText.find("Solicitando") != std::string::npos ||
                   statusText.find("Conectando") != std::string::npos);

    uint32_t pulseDot = isError ? 0xFFEF4444 : (isBusy ? (((frameCount / 20) % 2 == 0) ? 0xFFF59E0B : 0xFFD97706) : 0xFF02FDDB);
    fillRect(85, footerY + 22, 12, 12, pulseDot);

    uint32_t textColor = isError ? 0xFFFCA5A5 : (isBusy ? 0xFFFDE68A : 0xFF02FDDB);
    std::string stepInfo = luna::util::CrashLogger::getCurrentStepText();
    std::string line1 = "Amazon Luna: " + statusText;
    if (!stepInfo.empty() && isBusy && line1.find(stepInfo) == std::string::npos) {
        line1 += "  |  " + stepInfo;
    }
    drawText(108, footerY + 16, 2, utf8SafeTruncate(line1, 105), textColor);

    drawText(108, footerY + 50, 1,
             "[ARRIBA/ABAJO] Cambiar Fila   |   [IZQ/DER] Seleccionar   |   [L2/R2] Pestañas   |   [(X)] Detalles / Acción   |   [CUADRADO] Jugar ya   |   [TRIÁNGULO] Cerrar Sesión",
             0xFFB8B2C8);

    present();
}

void DisplayManager::drawCatalogScreen(const std::vector<GameTile>& games,
                                       int selectedIndex,
                                       const std::string& customerId,
                                       const std::string& statusText,
                                       uint64_t frameCount,
                                       const std::string& currentCategory) {
    if (!isReady_ || !mappedMemory_) return;

    // 1. Fondo principal: Obsidian Luna Dark Slate
    clear(0xFF090714);

    // 2. Barra de navegacion superior oficial estilo Amazon Luna Smart TV
    fillRect(0, 0, 1920, 72, 0xFF0D0A1C);
    fillRect(0, 72, 1920, 2, 0xFF241842);

    // Flechas de navegacion < >
    drawText(50, 22, 3, "<  >", 0xFF71717A);

    // Logotipo de Amazon Luna (▲ luna)
    fillRect(120, 28, 14, 14, 0xFF8B5CF6);
    drawText(142, 18, 5, "luna", 0xFFFFFFFF);

    // Pestañas de categorías funcionales con navegación L2 / R2
    drawText(265, 24, 2, "[L2]", 0xFFA78BFA);

    static const std::pair<const char*, const char*> kCategories[] = {
        {"TODOS", "TODOS LOS JUEGOS"},
        {"DESTACADOS", "DESTACADOS"},
        {"LUNA+", "LUNA+"},
        {"PRIME", "PRIME GAMING"},
        {"BIBLIOTECA", "MI BIBLIOTECA"}
    };

    int tabX = 315;
    for (const auto& cat : kCategories) {
        bool isActive = (currentCategory == cat.second || currentCategory == cat.first);
        int tabW = 20 + fontRenderer_.measureText(cat.first, 20);
        if (isActive) {
            fillRect(tabX, 18, tabW, 36, 0xFF6D28D9);
            drawText(tabX + 10, 24, 2, cat.first, 0xFFFFFFFF);
        } else {
            drawText(tabX + 10, 24, 2, cat.first, 0xFF9CA3AF);
        }
        tabX += tabW + 15;
    }

    drawText(tabX + 5, 24, 2, "[R2]", 0xFFA78BFA);

    // Indicadores a la derecha: Mando y Cuenta de usuario
    fillRect(1320, 28, 10, 10, 0xFF10B981);
    drawText(1338, 25, 2, "DualSense Conectado", 0xFF34D399);

    std::string userTag = customerId.empty() ? "Conectado" : "Usuario: " + customerId.substr(0, 14) + "...";
    drawText(1600, 25, 2, userTag, 0xFF38BDF8);

    // 3. Hero Showcase Banner (Juego destacado en foco)
    int heroX = 60;
    int heroY = 88;
    int heroW = 1800;
    int heroH = 415;

    drawCard(*this, heroX, heroY, heroW, heroH, 0xFF130E29, 0xFF2A1C52, 2);

    // Ilustracion estelar decorativa en el lado derecho del banner
    int planetCx = heroX + 1440;
    int planetCy = heroY + 200;
    for (int r = 160; r > 20; r -= 20) {
        uint32_t ringCol = (r > 120) ? 0xFF1D143D : ((r > 80) ? 0xFF311E63 : 0xFF4C1D95);
        fillRect(planetCx - r, planetCy - (r / 2), r * 2, r, ringCol);
    }
    // Anillo planetario inclinado
    fillRect(planetCx - 220, planetCy - 6, 440, 12, 0xFF7C3AED);
    fillRect(planetCx - 180, planetCy + 16, 360, 6, 0xFFF59E0B);
    // Lunas menores
    fillRect(planetCx - 240, planetCy - 90, 24, 24, 0xFFFDE68A);
    fillRect(planetCx + 180, planetCy + 110, 16, 16, 0xFFA78BFA);

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(games.size())) {
        const auto& cur = games[selectedIndex];

        // Badge de canal y editor
        fillRect(heroX + 50, heroY + 45, 12, 12, cur.badgeColor);
        drawText(heroX + 72, heroY + 41, 2, cur.publisher + "  |  " + cur.category, 0xFFF59E0B);

        // Titulo del juego en tamaño gigante
        drawText(heroX + 50, heroY + 75, 6, cur.title, 0xFFFFFFFF);

        // Sinopsis oficial
        drawText(heroX + 50, heroY + 160, 2, cur.synopsis, 0xFFE2E8F0);

        // Ficha tecnica del stream nativo PS5
        drawText(heroX + 50, heroY + 210, 2, "1080p 60 FPS   |   WebRTC Ultra-Low Latency   |   Audio 48 kHz Estereo", 0xFFA5B4FC);

        // Boton de accion interactivo (PULSA (X) PARA JUGAR)
        int btnX = heroX + 50;
        int btnY = heroY + 265;
        int btnW = 460;
        int btnH = 64;

        bool isStarting = (statusText.find("Asignando") != std::string::npos ||
                           statusText.find("Solicitando") != std::string::npos ||
                           statusText.find("Conectando") != std::string::npos);

        uint32_t btnBg = isStarting ? 0xFF047857 : 0xFF6D28D9;
        uint32_t btnBorder = isStarting ? 0xFF34D399 : 0xFFA78BFA;
        drawCard(*this, btnX, btnY, btnW, btnH, btnBg, btnBorder, 2);

        std::string btnText = isStarting ? "CONECTANDO A AMAZON..." : "PULSA (X) PARA JUGAR";
        drawText(btnX + 32, btnY + 18, 3, btnText, 0xFFFFFFFF);

        // Indicadores de carrusel (puntos paginadores)
        for (size_t d = 0; d < games.size() && d < 6; ++d) {
            int dotX = heroX + 50 + static_cast<int>(d) * 26;
            int dotY = heroY + 360;
            if (static_cast<int>(d) == selectedIndex) {
                fillRect(dotX, dotY, 20, 8, 0xFFFFFFFF);
            } else {
                fillRect(dotX, dotY, 8, 8, 0xFF475569);
            }
        }

        // Portada destacada del juego en el banner si esta disponible
        if (!cur.coverRgba.empty() && cur.coverWidth > 0 && cur.coverHeight > 0) {
            int heroCoverW = 250;
            int heroCoverH = 340;
            int heroCoverX = heroX + heroW - heroCoverW - 50;
            int heroCoverY = heroY + (heroH - heroCoverH) / 2;
            drawCard(*this, heroCoverX - 4, heroCoverY - 4, heroCoverW + 8, heroCoverH + 8, 0xFF0B0716, 0xFF8B5CF6, 2);
            drawImage(heroCoverX, heroCoverY, heroCoverW, heroCoverH, cur.coverRgba.data(), cur.coverWidth, cur.coverHeight);
        }
    }

    // 4. Seccion de Carrusel Dinamico con desplazamiento suave (todos los juegos de Amazon Luna)
    std::string catHeading = "Catalogo de Juegos (" + std::to_string(games.size()) + " disponibles)";
    drawText(60, 520, 3, catHeading, 0xFFFFFFFF);

    std::string pageInfo = "Juego " + std::to_string(selectedIndex + 1) + " de " + std::to_string(games.size()) +
                           "  |  [L1 / R1] Desplazar 5 juegos  |  Pulsa (X) para ver detalles y jugar";
    drawText(60, 552, 1, pageInfo, 0xFF94A3B8);

    int cardY = 578;
    int cardW = 320;
    int cardH = 390;
    int cardGap = 24;
    int visibleCount = 5;

    // Calcular scrollOffset dinamico para que el elemento seleccionado siempre sea visible
    int scrollOffset = 0;
    if (selectedIndex >= visibleCount) {
        scrollOffset = selectedIndex - visibleCount + 1;
    }
    if (scrollOffset + visibleCount > static_cast<int>(games.size())) {
        scrollOffset = static_cast<int>(games.size()) - visibleCount;
    }
    if (scrollOffset < 0) scrollOffset = 0;

    int totalVisible = std::min(visibleCount, static_cast<int>(games.size()) - scrollOffset);

    for (int vi = 0; vi < totalVisible; ++vi) {
        int i = scrollOffset + vi;
        int cardX = 60 + vi * (cardW + cardGap);
        bool isSelected = (i == selectedIndex);

        // Tarjeta vertical
        uint32_t cardBg = isSelected ? 0xFF1A1238 : 0xFF100B22;
        uint32_t cardBorder = isSelected ? 0xFF8B5CF6 : 0xFF241842;
        int borderWidth = isSelected ? 3 : 1;

        drawCard(*this, cardX, cardY, cardW, cardH, cardBg, cardBorder, borderWidth);

        // Barra superior con etiqueta de categoria y PEGI
        fillRect(cardX + borderWidth, cardY + borderWidth, cardW - (borderWidth * 2), 28, isSelected ? 0xFF2B1A56 : 0xFF130E26);
        fillRect(cardX + 14, cardY + 10, 8, 8, games[i].badgeColor);
        drawText(cardX + 28, cardY + 7, 1, games[i].category, isSelected ? 0xFFF59E0B : 0xFF94A3B8);
        drawText(cardX + cardW - 70, cardY + 7, 1, games[i].pegiRating, 0xFFA5B4FC);

        // Marco ilustrativo de portada de juego
        int artX = cardX + 14;
        int artY = cardY + 36;
        int artW = cardW - 28;
        int artH = 190;

        drawCard(*this, artX, artY, artW, artH, 0xFF0A0716, isSelected ? 0xFF6D28D9 : 0xFF1A1233, 1);

        if (!games[i].coverRgba.empty() && games[i].coverWidth > 0 && games[i].coverHeight > 0) {
            drawImage(artX + 1, artY + 1, artW - 2, artH - 2,
                      games[i].coverRgba.data(), games[i].coverWidth, games[i].coverHeight);
        } else {
            // Arte tematico distintivo y limpio basado en metadatos reales del juego
            fillRect(artX + 1, artY + 1, artW - 2, artH - 2, 0xFF130E26);
            fillRect(artX + 10, artY + 10, artW - 20, 6, games[i].badgeColor);

            std::string displayTitle = games[i].title;
            if (displayTitle.size() > 22) {
                displayTitle = displayTitle.substr(0, 20) + "...";
            }
            drawText(artX + 16, artY + 60, 3, displayTitle, 0xFFFFFFFF);
            drawText(artX + 16, artY + 110, 2, games[i].publisher, 0xFF94A3B8);
            drawText(artX + 16, artY + 145, 1, games[i].category + " | Cloud Stream", 0xFFA5B4FC);
        }

        // Informacion textual del juego bajo la caratula
        std::string cardTitle = games[i].title;
        if (cardTitle.size() > 24) {
            cardTitle = cardTitle.substr(0, 22) + "...";
        }
        drawText(cardX + 14, cardY + 238, 2, cardTitle, 0xFFFFFFFF);

        std::string cardPub = games[i].publisher;
        if (cardPub.size() > 28) {
            cardPub = cardPub.substr(0, 26) + "...";
        }
        drawText(cardX + 14, cardY + 276, 1, cardPub, 0xFF94A3B8);
        drawText(cardX + 14, cardY + 302, 1, "1080p 60 FPS | WebRTC", 0xFFA5B4FC);

        // Pie de tarjeta con estado interactivo
        if (isSelected) {
            fillRect(cardX + 14, cardY + 338, cardW - 28, 36, 0xFF6D28D9);
            drawText(cardX + 32, cardY + 346, 2, "PULSA (X) DETALLES", 0xFFFFFFFF);
        } else {
            drawText(cardX + 14, cardY + 348, 1, "Pulsa para seleccionar", 0xFF64748B);
        }
    }

    // 5. Barra inferior (Footer de diagnostico y navegacion en tiempo real)
    int footerY = 980;
    int footerH = 88;
    drawCard(*this, 60, footerY, 1800, footerH, 0xFF0D0A1C, 0xFF241842, 1);

    bool isError = (statusText.find("Error") != std::string::npos ||
                    statusText.find("fallo") != std::string::npos ||
                    statusText.find("Rechazado") != std::string::npos);

    bool isBusy = (statusText.find("Asignando") != std::string::npos ||
                   statusText.find("Solicitando") != std::string::npos ||
                   statusText.find("Conectando") != std::string::npos);

    uint32_t pulseDot = isError ? 0xFFEF4444 : (isBusy ? (((frameCount / 20) % 2 == 0) ? 0xFFF59E0B : 0xFFD97706) : 0xFF10B981);
    fillRect(85, footerY + 22, 14, 14, pulseDot);

    uint32_t textColor = isError ? 0xFFFCA5A5 : (isBusy ? 0xFFFDE68A : 0xFF34D399);
    std::string stepInfo = luna::util::CrashLogger::getCurrentStepText();
    std::string line1 = "Amazon Luna: " + statusText;
    if (!stepInfo.empty() && isBusy && line1.find(stepInfo) == std::string::npos) {
        line1 += "  |  " + stepInfo;
    }
    drawText(110, footerY + 18, 2, line1, textColor);

    drawText(110, footerY + 52, 1,
             "[D-PAD / Stick Izq] Seleccionar   |   [L1/R1] Pagina   |   [CRUZ (X)] Ficha y Jugar   |   [TRIANGULO] Cerrar Sesion   |   [L1+R1+OPTIONS+TOUCH] Salir",
             0xFFCBD5E1);

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

    // 1. Fondo principal oficial de Amazon Luna (#0E0B16 Cosmic Deep)
    clear(0xFF0E0B16);

    // 2. Barra de navegacion superior oficial estilo Amazon Luna
    fillRect(0, 0, 1920, 72, 0xFF130E21);
    fillRect(0, 72, 1920, 2, 0xFF281E42);

    // Boton Volver [O]
    drawCard(*this, 50, 16, 225, 40, 0xFF261E3A, 0xFF4C3B78, 1);
    drawText(64, 23, 2, "(O)  Volver", 0xFFFFFFFF);

    // Logotipo de Amazon Luna (▲ luna)
    fillRect(306, 26, 14, 14, 0xFF9146FF);
    fillRect(310, 30, 6, 6, 0xFF02FDDB);
    drawText(328, 16, 5, "luna", 0xFFFFFFFF);
    drawText(450, 27, 1, "DETALLES DEL JUEGO", 0xFFC8A2FF);

    // Indicadores a la derecha: Mando y Perfil del Jugador
    fillRect(1220, 30, 10, 10, 0xFF02FDDB);
    drawText(1236, 25, 1, "Mando DualSense activo", 0xFF02FDDB);

    drawCard(*this, 1420, 14, 440, 44, 0xFF1C162B, 0xFF382B54, 1);
    std::string pTag = profile.displayName + " " + profile.discriminator + "  •  " + profile.planStatus;
    drawText(1438, 24, 1, utf8SafeTruncate(pTag, 48), 0xFFFFFFFF);

    // 3. ZONA SUPERIOR: Ficha Principal del Juego (PRODUCT_DETAIL + GAME_SPECIFICATIONS)
    int cardX = 60;
    int cardY = 86;
    int cardW = 1800;
    int cardH = 526;
    drawCard(*this, cardX, cardY, cardW, cardH, 0xFF161126, (detailZone == 0) ? 0xFF9146FF : 0xFF2B2144, (detailZone == 0) ? 2 : 1);

    // Portada de alta resolucion en el lado izquierdo
    int coverX = cardX + 34;
    int coverY = cardY + 26;
    int coverW = 340;
    int coverH = 474;

    drawCard(*this, coverX - 3, coverY - 3, coverW + 6, coverH + 6, 0xFF0E0B16, 0xFF9146FF, 2);
    if (!game.coverRgba.empty() && game.coverWidth > 0 && game.coverHeight > 0) {
        drawImage(coverX, coverY, coverW, coverH, game.coverRgba.data(), game.coverWidth, game.coverHeight);
    } else {
        fillRect(coverX, coverY, coverW, 100, game.badgeColor);
        drawText(coverX + 20, coverY + 38, 3, game.category, 0xFFFFFFFF);
        fillRect(coverX, coverY + 100, coverW, coverH - 100, 0xFF130E21);
        drawText(coverX + 20, coverY + 200, 3, utf8SafeTruncate(game.title, 22), 0xFFFFFFFF);
        drawText(coverX + 20, coverY + 260, 2, utf8SafeTruncate(game.publisher, 26), 0xFFB8B2C8);
    }

    // Columna Derecha: Metadatos Oficiales, Descripcion Completa y Botones Tipo Pildora
    int infoX = coverX + coverW + 44;
    int infoY = cardY + 24;

    // Fila de Pills: Canal, PEGI, Año, Generos, Metacritic
    fillRect(infoX, infoY, 210, 30, 0xFF2D224C);
    fillRect(infoX, infoY, 6, 30, 0xFF02FDDB);
    drawText(infoX + 14, infoY + 6, 1, game.category, 0xFF02FDDB);

    fillRect(infoX + 222, infoY, 92, 30, 0xFF261E3A);
    drawText(infoX + 236, infoY + 6, 1, game.pegiRating, 0xFFFDE68A);

    fillRect(infoX + 326, infoY, 76, 30, 0xFF261E3A);
    drawText(infoX + 342, infoY + 6, 1, game.releaseYear.empty() ? "2025" : game.releaseYear, 0xFFFFFFFF);

    int genrePillW = std::min(440, 28 + fontRenderer_.measureText(game.genres, 16));
    fillRect(infoX + 414, infoY, genrePillW, 30, 0xFF1F1836);
    drawText(infoX + 428, infoY + 6, 1, utf8SafeTruncate(game.genres, 42), 0xFFC8A2FF);

    if (!game.metacritic.empty()) {
        fillRect(infoX + 426 + genrePillW, infoY, 150, 30, 0xFF065F46);
        drawText(infoX + 438 + genrePillW, infoY + 6, 1, game.metacritic, 0xFF6EE7B7);
    }

    // Titulo oficial del juego (Gigante)
    drawText(infoX, infoY + 40, 6, utf8SafeTruncate(game.title, 36), 0xFFFFFFFF);

    // Subtexto oficial de canal (item_game_detail_subtext_entitled_by_channel)
    drawText(infoX, infoY + 112, 2, "✓  " + game.channelSubtext, 0xFF02FDDB);

    // Barra de especificaciones tecnicas (Desarrollador, Distribuidora, Jugadores, 1080p60)
    fillRect(infoX, infoY + 148, 1340, 38, 0xFF1F1833);
    std::string devPubLine = "Desarrollador: " + (game.developer.empty() ? game.publisher : game.developer) +
                             "   |   Distribuidora: " + game.publisher +
                             "   |   " + game.playerCount +
                             "   |   1080p 60 FPS • " + game.inputMethods;
    drawText(infoX + 16, infoY + 158, 1, utf8SafeTruncate(devPubLine, 118), 0xFFE2E8F0);

    // Descripcion real completa del juego en español (envuelta limpiamente hasta 6 lineas)
    auto descLines = utf8WrapLines(game.synopsis, 96, 6);
    int synY = infoY + 200;
    for (const auto& ln : descLines) {
        drawText(infoX, synY, 2, ln, 0xFFCBD5E1);
        synY += 27;
    }

    // Botones oficiales tipo pildora en Zona 0 de Detalle: [▶ Jugar ahora] y [Volver al catálogo]
    int btnPlayX = infoX;
    int btnPlayY = cardY + 432;
    int btnPlayW = 420;
    int btnPlayH = 64;
    bool playFocused = (detailZone == 0 && detailBtnIdx == 0);

    drawCard(*this, btnPlayX, btnPlayY, btnPlayW, btnPlayH,
             isConnecting ? 0xFF047857 : (playFocused ? 0xFF9146FF : 0xFF5B21B6),
             playFocused ? 0xFFFFFFFF : 0xFF9146FF, playFocused ? 3 : 1);
    drawText(btnPlayX + 32, btnPlayY + 18, 3,
             isConnecting ? "CONECTANDO A LUNA..." : "▶  Jugar ahora", 0xFFFFFFFF);

    int btnBackX = btnPlayX + btnPlayW + 24;
    int btnBackY = cardY + 432;
    int btnBackW = 340;
    int btnBackH = 64;
    bool backFocused = (detailZone == 0 && detailBtnIdx == 1);

    drawCard(*this, btnBackX, btnBackY, btnBackW, btnBackH,
             backFocused ? 0xFF3B2D5E : 0xFF261E3A,
             backFocused ? 0xFFFFFFFF : 0xFF4C3B78, backFocused ? 3 : 1);
    drawText(btnBackX + 32, btnBackY + 18, 3, "Volver al catálogo", 0xFFFFFFFF);

    // 4. ZONA INFERIOR: Carrusel Oficial "También te puede interesar" (collection_similar_products)
    int simY = 626;
    drawText(60, simY, 3, "También te puede interesar", (detailZone == 1) ? 0xFFFFFFFF : 0xFFC8BFE7);
    drawText(460, simY + 6, 1,
             (detailZone == 1)
                 ? "[IZQ / DER] Explorar juegos similares   |   [(X)] Abrir juego seleccionado   |   [ARRIBA] Volver a Jugar ahora"
                 : "Pulsa [ABAJO] para explorar juegos similares recomendados por Amazon Luna",
             (detailZone == 1) ? 0xFF02FDDB : 0xFF9F8FEF);

    int simCardY = simY + 36;
    int simCardW = 280;
    int simCardH = 300;
    int simGap = 24;
    int simVisible = 6;
    int simTotal = static_cast<int>(game.similarGames.size());

    if (simTotal > 0) {
        int clampedSim = std::max(0, std::min(similarSelectedIdx, simTotal - 1));
        int scrollSim = 0;
        if (clampedSim >= simVisible) scrollSim = clampedSim - simVisible + 1;
        if (scrollSim + simVisible > simTotal) scrollSim = simTotal - simVisible;
        if (scrollSim < 0) scrollSim = 0;

        int drawCount = std::min(simVisible, simTotal - scrollSim);
        for (int vi = 0; vi < drawCount; ++vi) {
            int idx = scrollSim + vi;
            int sx = 60 + vi * (simCardW + simGap);
            bool isSimFocused = (detailZone == 1 && idx == clampedSim);
            const auto& sim = game.similarGames[idx];

            drawCard(*this, sx, simCardY, simCardW, simCardH,
                     isSimFocused ? 0xFF261E3A : 0xFF161126,
                     isSimFocused ? 0xFFFFFFFF : 0xFF2B2144,
                     isSimFocused ? 3 : 1);

            int sArtX = sx + 10;
            int sArtY = simCardY + 10;
            int sArtW = simCardW - 20;
            int sArtH = 175;
            drawCard(*this, sArtX, sArtY, sArtW, sArtH, 0xFF0E0B16, isSimFocused ? 0xFF9146FF : 0xFF241B3B, 1);

            if (!sim.coverRgba.empty() && sim.coverWidth > 0 && sim.coverHeight > 0) {
                drawImage(sArtX + 1, sArtY + 1, sArtW - 2, sArtH - 2,
                          sim.coverRgba.data(), sim.coverWidth, sim.coverHeight);
            } else {
                fillRect(sArtX + 1, sArtY + 1, sArtW - 2, sArtH - 2, 0xFF151026);
                fillRect(sArtX + 10, sArtY + 10, sArtW - 20, 5, sim.badgeColor);
                drawText(sArtX + 12, sArtY + 50, 2, utf8SafeTruncate(sim.title, 20), 0xFFFFFFFF);
                drawText(sArtX + 12, sArtY + 95, 1, utf8SafeTruncate(sim.publisher, 24), 0xFFB8B2C8);
            }

            drawText(sx + 12, simCardY + 194, 2, utf8SafeTruncate(sim.title, 21), 0xFFFFFFFF);
            drawText(sx + 12, simCardY + 222, 1, utf8SafeTruncate(sim.publisher + " • " + sim.pegiRating, 28), 0xFFB8B2C8);

            if (isSimFocused) {
                fillRect(sx + 10, simCardY + 254, simCardW - 20, 34, 0xFF9146FF);
                drawText(sx + 24, simCardY + 262, 1, "(X) Ver detalles del juego", 0xFFFFFFFF);
            } else {
                drawText(sx + 12, simCardY + 260, 1, utf8SafeTruncate(sim.genres, 28), 0xFFC8A2FF);
            }
        }
    } else {
        drawCard(*this, 60, simCardY, 1800, 180, 0xFF130E21, 0xFF281E42, 1);
        drawText(90, simCardY + 75, 2, "Explora más títulos recomendados en las filas del catálogo principal de Amazon Luna.", 0xFFB8B2C8);
    }

    // 5. Barra inferior (Footer de diagnostico y estado)
    int footerY = 980;
    int footerH = 86;
    drawCard(*this, 60, footerY, 1800, footerH, 0xFF130E21, 0xFF281E42, 1);

    bool isError = (statusText.find("Error") != std::string::npos ||
                    statusText.find("Fallo") != std::string::npos);
    uint32_t pulseDot = isError ? 0xFFEF4444 : (isConnecting ? (((frameCount / 20) % 2 == 0) ? 0xFFF59E0B : 0xFFD97706) : 0xFF02FDDB);
    fillRect(85, footerY + 22, 12, 12, pulseDot);

    uint32_t textColor = isError ? 0xFFFCA5A5 : (isConnecting ? 0xFFFDE68A : 0xFF02FDDB);
    drawText(108, footerY + 16, 2, utf8SafeTruncate("Amazon Luna: " + statusText, 105), textColor);
    drawText(108, footerY + 50, 1,
             "[IZQ/DER] Cambiar botón o juego similar   |   [ARRIBA/ABAJO] Cambiar zona   |   [(X)] Confirmar   |   [(O)] Volver al catálogo",
             0xFFB8B2C8);

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

        fillRect(ovX + 36, ovY + 28, 16, 16, 0xFF9146FF);
        fillRect(ovX + 40, ovY + 32, 8, 8, 0xFF02FDDB);
        drawText(ovX + 62, ovY + 18, 5, "luna", 0xFFFFFFFF);
        drawText(ovX + 185, ovY + 29, 2, "MENÚ DE PARTIDA EN LA NUBE", 0xFF02FDDB);

        if (!gameTitle.empty()) {
            drawText(ovX + 36, ovY + 94, 3, utf8SafeTruncate("Jugando a: " + gameTitle, 48), 0xFFFFFFFF);
        }

        // Panel de telemetria en tiempo real
        drawCard(*this, ovX + 36, ovY + 140, ovW - 72, 76, 0xFF1C162B, 0xFF2D224C, 1);
        drawText(ovX + 54, ovY + 152, 2, "Estado del Stream: 1080p 60 FPS Full HD  •  WebRTC Conectado", 0xFF02FDDB);
        drawText(ovX + 54, ovY + 182, 1, utf8SafeTruncate(hudLine1, 92), 0xFFC8A2FF);

        // 3 Botones interactivos del Overlay
        const std::string btnLabels[3] = {
            "▶   Reanudar partida",
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
