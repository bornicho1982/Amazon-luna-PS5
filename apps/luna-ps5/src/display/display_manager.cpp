#include "display/display_manager.h"
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

    for (char ch : text) {
        if (ch == '\n') {
            cursorX = x;
            cursorY += lineHeight;
            continue;
        }

        uint32_t codepoint = static_cast<uint8_t>(ch);
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

void DisplayManager::drawCatalogScreen(const std::vector<GameTile>& games,
                                       int selectedIndex,
                                       const std::string& customerId,
                                       const std::string& statusText,
                                       uint64_t frameCount) {
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

    // Pestañas principales
    // Inicio (Pill activa con fondo violeta sutil)
    fillRect(290, 18, 86, 36, 0xFF281C4F);
    drawText(308, 24, 2, "Inicio", 0xFFFFFFFF);

    drawText(410, 24, 2, "GameNight", 0xFF9CA3AF);
    drawText(540, 24, 2, "Mi espacio", 0xFF9CA3AF);

    drawText(680, 24, 2, "Remote Play", 0xFF9CA3AF);
    fillRect(805, 22, 60, 22, 0xFF1D4ED8);
    drawText(812, 26, 1, "NUEVO", 0xFF93C5FD);

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
    }

    // 4. Seccion de Carrusel: "Las mejores novedades en juegos"
    drawText(60, 520, 3, "Las mejores novedades en juegos", 0xFFFFFFFF);
    drawText(60, 552, 1, "Juegos incluidos y disponibles al instante en tu suscripcion", 0xFF94A3B8);

    int cardY = 578;
    int cardW = 280;
    int cardH = 390;
    int cardGap = 20;

    for (size_t i = 0; i < games.size() && i < 6; ++i) {
        int cardX = 60 + static_cast<int>(i) * (cardW + cardGap);
        bool isSelected = (static_cast<int>(i) == selectedIndex);

        // Numero de ranking en marca de agua de fondo (1, 2, 3...)
        drawText(cardX + 10, cardY + 8, 6, std::to_string(i + 1), isSelected ? 0xFF38235E : 0xFF1C1333);

        // Tarjeta vertical
        uint32_t cardBg = isSelected ? 0xFF171033 : 0xFF100B22;
        uint32_t cardBorder = isSelected ? 0xFF8B5CF6 : 0xFF241842;
        int borderWidth = isSelected ? 3 : 1;

        drawCard(*this, cardX, cardY, cardW, cardH, cardBg, cardBorder, borderWidth);

        // Barra superior con etiqueta de categoria
        fillRect(cardX + borderWidth, cardY + borderWidth, cardW - (borderWidth * 2), 28, isSelected ? 0xFF2B1A56 : 0xFF130E26);
        fillRect(cardX + 14, cardY + 10, 8, 8, games[i].badgeColor);
        drawText(cardX + 28, cardY + 7, 1, games[i].category, isSelected ? 0xFFF59E0B : 0xFF94A3B8);

        // Marco ilustrativo de portada de juego
        int artX = cardX + 14;
        int artY = cardY + 36;
        int artW = cardW - 28;
        int artH = 190;

        drawCard(*this, artX, artY, artW, artH, 0xFF0A0716, isSelected ? 0xFF6D28D9 : 0xFF1A1233, 1);

        // Arte tematico distintivo por juego
        switch (i) {
            case 0: { // Fortnite
                fillRect(artX, artY, artW, 80, 0xFF0284C7);
                fillRect(artX + 30, artY + 40, artW - 60, 8, 0xFF38BDF8);
                fillRect(artX + artW / 2 - 20, artY + 70, 40, 40, 0xFFF59E0B);
                drawText(artX + 35, artY + 130, 3, "FORTNITE", 0xFFFFFFFF);
                break;
            }
            case 1: { // Star Wars Outlaws
                fillRect(artX, artY, artW, artH, 0xFF181026);
                fillRect(artX + artW / 2 - 40, artY + 30, 80, 80, 0xFFD97706);
                fillRect(artX + 15, artY + 100, artW - 30, 6, 0xFFF59E0B);
                drawText(artX + 35, artY + 125, 3, "OUTLAWS", 0xFFFFFFFF);
                break;
            }
            case 2: { // Avatar: Frontiers of Pandora
                fillRect(artX, artY, artW, artH, 0xFF064E3B);
                fillRect(artX + 20, artY + 30, artW - 40, 60, 0xFF059669);
                fillRect(artX + 30, artY + 70, artW - 60, 4, 0xFF34D399);
                drawText(artX + 45, artY + 125, 3, "AVATAR", 0xFFFFFFFF);
                break;
            }
            case 3: { // Trackmania
                fillRect(artX, artY, artW, artH, 0xFF0E7490);
                fillRect(artX + 10, artY + 50, artW - 20, 16, 0xFF22D3EE);
                fillRect(artX + 30, artY + 75, artW - 60, 16, 0xFFFFFFFF);
                drawText(artX + 15, artY + 125, 3, "TRACKMANIA", 0xFFFFFFFF);
                break;
            }
            case 4: { // Sonic x Shadow
                fillRect(artX, artY, artW, artH, 0xFF881337);
                fillRect(artX + 20, artY + 40, artW - 40, 6, 0xFFEF4444);
                fillRect(artX + 35, artY + 60, artW - 70, 6, 0xFFF43F5E);
                drawText(artX + 45, artY + 125, 3, "SHADOW", 0xFFFFFFFF);
                break;
            }
            case 5: { // Fallout: New Vegas
                fillRect(artX, artY, artW, artH, 0xFF581C87);
                fillRect(artX + 25, artY + 30, artW - 50, 70, 0xFF7E22CE);
                fillRect(artX + 40, artY + 70, artW - 80, 6, 0xFFC084FC);
                drawText(artX + 25, artY + 125, 3, "NEW VEGAS", 0xFFFFFFFF);
                break;
            }
            default:
                drawText(artX + 20, artY + 100, 3, "LUNA", 0xFFFFFFFF);
                break;
        }

        // Informacion textual del juego
        drawText(cardX + 14, cardY + 242, 2, games[i].title, 0xFFFFFFFF);
        drawText(cardX + 14, cardY + 280, 1, games[i].publisher, 0xFF94A3B8);
        drawText(cardX + 14, cardY + 304, 1, "1080p 60 FPS | WebRTC", 0xFFA5B4FC);

        // Pie de tarjeta con estado interactivo
        if (isSelected) {
            fillRect(cardX + 14, cardY + 338, cardW - 28, 36, 0xFF10B981);
            drawText(cardX + 40, cardY + 346, 2, "PULSA (X) PARA JUGAR", 0xFFFFFFFF);
        } else {
            drawText(cardX + 14, cardY + 348, 1, "Disponible en tu cuenta", 0xFF64748B);
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
    drawText(110, footerY + 18, 2, "Amazon Luna: " + statusText, textColor);

    drawText(110, footerY + 52, 1,
             "[D-PAD / Stick Izq] Seleccionar Juego   |   [CRUZ (X)] Jugar en la Nube   |   [TRIANGULO] Cerrar Sesion   |   [L1+R1+OPTIONS+TOUCH] Salir",
             0xFFCBD5E1);

    present();
}

bool DisplayManager::presentYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                  int strideY, int strideU, int strideV,
                                  int width, int height) {
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
