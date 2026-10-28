#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "util/stb_image.h"
#include "util/image_loader.h"
#include "net/http_client.h"
#include "util/crash_logger.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <chrono>

namespace luna::util {

bool decodeImageRgba(const uint8_t* data, size_t size, std::vector<uint8_t>& outRgba, int& outW, int& outH) {
    if (!data || size == 0 || size > 4 * 1024 * 1024) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char* decoded = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
    if (!decoded) return false;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        stbi_image_free(decoded);
        return false;
    }
    outW = w;
    outH = h;
    outRgba.assign(decoded, decoded + (static_cast<size_t>(w) * static_cast<size_t>(h) * 4));
    stbi_image_free(decoded);
    return true;
}

std::string sanitizeAmazonImageUrl(const std::string& url) {
    std::string u = url;
    size_t p = 0;
    while ((p = u.find("_FMwebp_")) != std::string::npos) {
        u.replace(p, 8, "_FMjpg_");
    }
    size_t w = u.find(".webp");
    while (w != std::string::npos) {
        size_t after = w + 5;
        if (after == u.size() || u[after] == '?' || u[after] == '&') {
            u.replace(w, 5, ".jpg");
            w = u.find(".webp", w + 4);
        } else {
            w = u.find(".webp", w + 5);
        }
    }
    return u;
}

std::string optimizeAmazonImageUrl(const std::string& url, ImagePreset preset) {
    if (preset == ImagePreset::Raw || preset == ImagePreset::ProductLogo) return url;
    static const std::string kMarker = "media-amazon.com/images/I/";
    size_t m = url.find(kMarker);
    if (m == std::string::npos) return url;
    size_t idStart = m + kMarker.size();
    if (url.find("._", idStart) != std::string::npos) return url;   // already carries a size modifier
    if (url.find('/', idStart) != std::string::npos) return url;
    size_t dot = url.find('.', idStart);
    if (dot == std::string::npos) return url;
    std::string ext = url.substr(dot + 1);
    size_t q = ext.find('?');
    if (q != std::string::npos) ext = ext.substr(0, q);
    if (ext != "png" && ext != "jpg" && ext != "jpeg") return url;

    int px = 0;
    switch (preset) {
        case ImagePreset::VerticalPoster: px = 400; break;
        case ImagePreset::ScreenshotThumbnail: px = 540; break;
        case ImagePreset::HorizontalCard16_9: px = 600; break;
        case ImagePreset::SquareCategory: px = 360; break;
        case ImagePreset::PanoramicHero: px = 1920; break;
        case ImagePreset::PanoramicDetail: px = 1920; break;
        default: return url;
    }
    return url.substr(0, dot) + "._UX" + std::to_string(px) + "_" + url.substr(dot);
}

namespace {

inline void applyCornerMask(uint8_t* rgba, int w, int h, int r) {
    if (r <= 0 || !rgba || w <= 0 || h <= 0) return;
    float rf = static_cast<float>(r);
    size_t totalBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    for (int y = 0; y < h; ++y) {
        int cy = -1;
        if (y < r) cy = r;
        else if (y >= h - r) cy = h - 1 - r;
        if (cy == -1) continue;

        for (int x = 0; x < w; ++x) {
            int cx = -1;
            if (x < r) cx = r;
            else if (x >= w - r) cx = w - 1 - r;
            if (cx == -1) continue;

            float dx = static_cast<float>(x - cx);
            float dy = static_cast<float>(y - cy);
            float dist = std::sqrt(dx * dx + dy * dy);

            size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            if (offset + 3 < totalBytes) {
                uint8_t* p = rgba + offset;
                if (dist > rf) {
                    p[3] = 0;
                } else if (dist > rf - 1.0f) {
                    float factor = 1.0f - (dist - (rf - 1.0f));
                    p[3] = static_cast<uint8_t>(p[3] * factor);
                }
            }
        }
    }
}

inline void applyDarkGradient(uint8_t* rgba, int w, int h, int leftFadeW, int bottomFadeH) {
    if (!rgba || w <= 0 || h <= 0) return;
    const uint8_t bgR = 0x0E;
    const uint8_t bgG = 0x0B;
    const uint8_t bgB = 0x16;
    size_t totalBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;

    for (int y = 0; y < h; ++y) {
        float bottomFactor = 1.0f;
        if (bottomFadeH > 0 && y >= (h - bottomFadeH)) {
            float distFromBottom = static_cast<float>(h - 1 - y);
            bottomFactor = std::max(0.0f, distFromBottom / static_cast<float>(bottomFadeH));
            bottomFactor = bottomFactor * bottomFactor; // Ease in
        }

        for (int x = 0; x < w; ++x) {
            float leftFactor = 1.0f;
            if (leftFadeW > 0 && x < leftFadeW) {
                leftFactor = static_cast<float>(x) / static_cast<float>(leftFadeW);
                leftFactor = leftFactor * leftFactor; // Ease in
            }

            float alphaFactor = std::min(leftFactor, bottomFactor);
            size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            if (offset + 3 < totalBytes) {
                uint8_t* p = rgba + offset;
                p[0] = static_cast<uint8_t>(p[0] * alphaFactor + bgR * (1.0f - alphaFactor));
                p[1] = static_cast<uint8_t>(p[1] * alphaFactor + bgG * (1.0f - alphaFactor));
                p[2] = static_cast<uint8_t>(p[2] * alphaFactor + bgB * (1.0f - alphaFactor));
            }
        }
    }
}

} // namespace

bool resizeAndProcessImage(const uint8_t* srcRgba, int srcW, int srcH,
                           std::vector<uint8_t>& dstRgba, int dstW, int dstH,
                           ImagePreset preset) {
    if (!srcRgba || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return false;
    if (srcW > 4096 || srcH > 4096 || dstW > 4096 || dstH > 4096) return false;

    size_t totalDstBytes = static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4;
    dstRgba.assign(totalDstBytes, 0);

    const uint8_t bgR = 0x0E;
    const uint8_t bgG = 0x0B;
    const uint8_t bgB = 0x16;

    bool isPanoramic = (preset == ImagePreset::PanoramicHero || preset == ImagePreset::PanoramicDetail);

    if (isPanoramic && srcW < srcH) {
        // IMAGEN VERTICAL (POSTER 3:4): NO estirar a 16:9 deformandola
        for (size_t i = 0; i + 3 < totalDstBytes; i += 4) {
            dstRgba[i + 0] = bgR;
            dstRgba[i + 1] = bgG;
            dstRgba[i + 2] = bgB;
            dstRgba[i + 3] = 255;
        }

        int fitH = static_cast<int>(dstH * 0.95f);
        if (fitH <= 0) fitH = dstH;
        int fitW = static_cast<int>(fitH * (static_cast<float>(srcW) / srcH));
        if (fitW > dstW / 2) fitW = dstW / 2;
        if (fitW <= 0) fitW = 1;
        int posX = dstW - fitW - 80;
        int posY = (dstH - fitH) / 2;

        float scaleX = static_cast<float>(srcW) / static_cast<float>(fitW);
        float scaleY = static_cast<float>(srcH) / static_cast<float>(fitH);
        int fadeBorder = 50;

        for (int dy = 0; dy < fitH; ++dy) {
            int targetY = posY + dy;
            if (targetY < 0 || targetY >= dstH) continue;

            float sy = (dy + 0.5f) * scaleY - 0.5f;
            int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, srcH - 1);
            int y1 = std::clamp(y0 + 1, 0, srcH - 1);
            float fy = std::clamp(sy - static_cast<float>(y0), 0.0f, 1.0f);

            const uint8_t* row0 = srcRgba + (static_cast<size_t>(y0) * static_cast<size_t>(srcW) * 4);
            const uint8_t* row1 = srcRgba + (static_cast<size_t>(y1) * static_cast<size_t>(srcW) * 4);

            float edgeFadeY = 1.0f;
            if (dy < fadeBorder) edgeFadeY = static_cast<float>(dy) / fadeBorder;
            else if (dy >= fitH - fadeBorder) edgeFadeY = static_cast<float>(fitH - 1 - dy) / fadeBorder;

            for (int dx = 0; dx < fitW; ++dx) {
                int targetX = posX + dx;
                if (targetX < 0 || targetX >= dstW) continue;

                float sx = (dx + 0.5f) * scaleX - 0.5f;
                int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, srcW - 1);
                int x1 = std::clamp(x0 + 1, 0, srcW - 1);
                float fx = std::clamp(sx - static_cast<float>(x0), 0.0f, 1.0f);

                const uint8_t* p00 = row0 + (x0 * 4);
                const uint8_t* p01 = row0 + (x1 * 4);
                const uint8_t* p10 = row1 + (x0 * 4);
                const uint8_t* p11 = row1 + (x1 * 4);

                float w00 = (1.0f - fx) * (1.0f - fy);
                float w01 = fx * (1.0f - fy);
                float w10 = (1.0f - fx) * fy;
                float w11 = fx * fy;

                uint8_t r = static_cast<uint8_t>(p00[0] * w00 + p01[0] * w01 + p10[0] * w10 + p11[0] * w11);
                uint8_t g = static_cast<uint8_t>(p00[1] * w00 + p01[1] * w01 + p10[1] * w10 + p11[1] * w11);
                uint8_t b = static_cast<uint8_t>(p00[2] * w00 + p01[2] * w01 + p10[2] * w10 + p11[2] * w11);

                float edgeFadeX = 1.0f;
                if (dx < fadeBorder) edgeFadeX = static_cast<float>(dx) / fadeBorder;
                else if (dx >= fitW - fadeBorder) edgeFadeX = static_cast<float>(fitW - 1 - dx) / fadeBorder;

                float alphaFade = edgeFadeX * edgeFadeY;
                size_t outOffset = (static_cast<size_t>(targetY) * static_cast<size_t>(dstW) + static_cast<size_t>(targetX)) * 4;
                if (outOffset + 3 < totalDstBytes) {
                    dstRgba[outOffset + 0] = static_cast<uint8_t>(r * alphaFade + bgR * (1.0f - alphaFade));
                    dstRgba[outOffset + 1] = static_cast<uint8_t>(g * alphaFade + bgG * (1.0f - alphaFade));
                    dstRgba[outOffset + 2] = static_cast<uint8_t>(b * alphaFade + bgB * (1.0f - alphaFade));
                    dstRgba[outOffset + 3] = 255;
                }
            }
        }
        return true;
    }

    // Aspect-Fill si es panoramico
    float cropX = 0.0f;
    float cropY = 0.0f;
    float scaleX = static_cast<float>(srcW) / static_cast<float>(dstW);
    float scaleY = static_cast<float>(srcH) / static_cast<float>(dstH);

    if (isPanoramic) {
        float fillScale = std::max(static_cast<float>(dstW) / srcW, static_cast<float>(dstH) / srcH);
        float cropW = dstW / fillScale;
        float cropH = dstH / fillScale;
        cropX = (srcW - cropW) * 0.5f;
        cropY = (srcH - cropH) * 0.5f;
        scaleX = 1.0f / fillScale;
        scaleY = 1.0f / fillScale;
    }

    for (int dy = 0; dy < dstH; ++dy) {
        float sy = isPanoramic ? (cropY + (dy + 0.5f) * scaleY - 0.5f) : ((dy + 0.5f) * scaleY - 0.5f);
        int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, srcH - 1);
        int y1 = std::clamp(y0 + 1, 0, srcH - 1);
        float fy = std::clamp(sy - static_cast<float>(y0), 0.0f, 1.0f);

        const uint8_t* row0 = srcRgba + (static_cast<size_t>(y0) * static_cast<size_t>(srcW) * 4);
        const uint8_t* row1 = srcRgba + (static_cast<size_t>(y1) * static_cast<size_t>(srcW) * 4);

        for (int dx = 0; dx < dstW; ++dx) {
            float sx = isPanoramic ? (cropX + (dx + 0.5f) * scaleX - 0.5f) : ((dx + 0.5f) * scaleX - 0.5f);
            int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, srcW - 1);
            int x1 = std::clamp(x0 + 1, 0, srcW - 1);
            float fx = std::clamp(sx - static_cast<float>(x0), 0.0f, 1.0f);

            const uint8_t* p00 = row0 + (x0 * 4);
            const uint8_t* p01 = row0 + (x1 * 4);
            const uint8_t* p10 = row1 + (x0 * 4);
            const uint8_t* p11 = row1 + (x1 * 4);

            float w00 = (1.0f - fx) * (1.0f - fy);
            float w01 = fx * (1.0f - fy);
            float w10 = (1.0f - fx) * fy;
            float w11 = fx * fy;

            size_t outOffset = (static_cast<size_t>(dy) * static_cast<size_t>(dstW) + static_cast<size_t>(dx)) * 4;
            if (outOffset + 3 < totalDstBytes) {
                dstRgba[outOffset + 0] = static_cast<uint8_t>(p00[0] * w00 + p01[0] * w01 + p10[0] * w10 + p11[0] * w11);
                dstRgba[outOffset + 1] = static_cast<uint8_t>(p00[1] * w00 + p01[1] * w01 + p10[1] * w10 + p11[1] * w11);
                dstRgba[outOffset + 2] = static_cast<uint8_t>(p00[2] * w00 + p01[2] * w01 + p10[2] * w10 + p11[2] * w11);
                dstRgba[outOffset + 3] = static_cast<uint8_t>(p00[3] * w00 + p01[3] * w01 + p10[3] * w10 + p11[3] * w11);
            }
        }
    }

    switch (preset) {
        case ImagePreset::VerticalPoster:
            applyCornerMask(dstRgba.data(), dstW, dstH, 14);
            break;
        case ImagePreset::ScreenshotThumbnail:
            applyCornerMask(dstRgba.data(), dstW, dstH, 10);
            break;
        case ImagePreset::HorizontalCard16_9:
            applyCornerMask(dstRgba.data(), dstW, dstH, 12);
            break;
        case ImagePreset::SquareCategory:
            applyCornerMask(dstRgba.data(), dstW, dstH, 14);
            break;
        case ImagePreset::PanoramicHero:
            applyDarkGradient(dstRgba.data(), dstW, dstH, 850, 160);
            break;
        case ImagePreset::PanoramicDetail:
            applyDarkGradient(dstRgba.data(), dstW, dstH, 850, 220);
            break;
        case ImagePreset::ProductLogo:
        case ImagePreset::Raw:
        default:
            break;
    }

    return true;
}

ImageLoader& ImageLoader::instance() {
    static ImageLoader s_inst;
    return s_inst;
}

ImageLoader::ImageLoader() = default;

ImageLoader::~ImageLoader() {
    shutdown();
}

#if !defined(_WIN32)
extern "C" {
    int scePthreadAttrInit(void* attr);
    int scePthreadAttrSetstacksize(void* attr, size_t stacksize);
    int scePthreadAttrDestroy(void* attr);
    int scePthreadCreate(void** thread, const void* attr, void* (*entry)(void*), void* arg, const char* name);
    int scePthreadJoin(void* thread, void** value_ptr);
}

void* ImageLoader::pthreadEntry(void* arg) {
    if (arg) {
        static_cast<ImageLoader*>(arg)->workerLoop();
    }
    return nullptr;
}
#endif

void ImageLoader::init() {
    if (running_) return;
    running_ = true;

#if defined(_WIN32)
    try {
        workerThread_ = std::thread(&ImageLoader::workerLoop, this);
    } catch (...) {
        running_ = false;
        luna::util::CrashLogger::log("[ImageLoader ERROR] No se pudo crear el hilo worker.");
        return;
    }
    luna::util::CrashLogger::log("[ImageLoader] Hilo worker de descarga iniciado (Win32 std::thread).");
#else
    alignas(16) uint8_t attr[256];
    std::memset(attr, 0, sizeof(attr));
    int rcAttr = scePthreadAttrInit(attr);
    if (rcAttr == 0) {
        scePthreadAttrSetstacksize(attr, 2 * 1024 * 1024);
    }
    nativeThread_ = nullptr;
    int rc = scePthreadCreate(&nativeThread_, attr, &ImageLoader::pthreadEntry, this, "image_worker");
    scePthreadAttrDestroy(attr);
    if (rc != 0 || !nativeThread_) {
        running_ = false;
        luna::util::CrashLogger::log("[ImageLoader ERROR] scePthreadCreate fallo: %d", rc);
        return;
    }
    luna::util::CrashLogger::log("[ImageLoader] Hilo worker de descarga iniciado con stack dedicado de 2 MB (scePthreadCreate).");
#endif
}

void ImageLoader::shutdown() {
    if (!running_) return;
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        running_ = false;
        queue_.clear();
    }
    cv_.notify_all();
#if defined(_WIN32)
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
#else
    if (nativeThread_) {
        scePthreadJoin(nativeThread_, nullptr);
        nativeThread_ = nullptr;
    }
#endif
}

static inline std::string getCacheKey(const std::string& url, ImagePreset preset) {
    return url + "@" + std::to_string(static_cast<int>(preset));
}

std::shared_ptr<const DecodedImage> ImageLoader::getImage(const std::string& urlIn, ImagePreset preset) {
    if (urlIn.empty()) return nullptr;
    std::string url = sanitizeAmazonImageUrl(urlIn);
    std::string key = getCacheKey(url, preset);

    std::lock_guard<std::mutex> lk(queueMutex_);
    for (size_t i = 0; i < MAX_IMAGE_SLOTS; ++i) {
        if (slots_[i].state.load(std::memory_order_acquire) == 2 && slots_[i].key == key) {
            slots_[i].lastUse = ++tick_;
            return slots_[i].image;
        }
    }

    if (preset == ImagePreset::Raw) {
        for (size_t i = 0; i < MAX_IMAGE_SLOTS; ++i) {
            if (slots_[i].state.load(std::memory_order_acquire) == 2) {
                if (slots_[i].key.rfind(url + "@", 0) == 0) {
                    slots_[i].lastUse = ++tick_;
                    return slots_[i].image;
                }
            }
        }
    }
    return nullptr;
}

void ImageLoader::requestImage(const std::string& urlIn, ImagePreset preset) {
    if (urlIn.empty() || urlIn.rfind("http", 0) != 0) return;
    std::string url = sanitizeAmazonImageUrl(urlIn);
    std::string key = getCacheKey(url, preset);
    const uint64_t nowMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());

    std::unique_lock<std::mutex> lk(queueMutex_);
    int target = -1;
    int freeSlot = -1;
    int failedSlot = -1;
    int lruSlot = -1;
    uint64_t lruTick = UINT64_MAX;
    for (size_t i = 0; i < MAX_IMAGE_SLOTS; ++i) {
        int st = slots_[i].state.load(std::memory_order_acquire);
        if (st == 2 || st == 1) {
            if (slots_[i].key == key) {
                return; // Ya encolada, descargando o lista
            }
            if (st == 2 && slots_[i].lastUse < lruTick) {
                lruTick = slots_[i].lastUse;
                lruSlot = static_cast<int>(i);
            }
        } else if (st == -1) {
            if (slots_[i].key == key) {
                if (nowMs - slots_[i].failMs < 20000) return; // back-off before retrying a failed download
                target = static_cast<int>(i);
                break;
            }
            if (failedSlot < 0) failedSlot = static_cast<int>(i);
        } else if (freeSlot < 0) {
            freeSlot = static_cast<int>(i);
        }
    }

    if (target < 0) target = freeSlot;
    if (target < 0) target = failedSlot;
    if (target < 0) target = lruSlot; // evict least-recently-used ready image
    if (target < 0) return;           // every slot is queued/downloading right now

    slots_[target].key = key;
    slots_[target].image.reset();
    slots_[target].lastUse = ++tick_;
    slots_[target].state.store(1, std::memory_order_release);

    queue_.push_back(Request{url, preset, target});
    lk.unlock();
    cv_.notify_one();
}

void ImageLoader::prioritizeOrClearPending() {
    std::lock_guard<std::mutex> lk(queueMutex_);
    for (const auto& r : queue_) {
        if (r.slotIdx >= 0 && r.slotIdx < static_cast<int>(MAX_IMAGE_SLOTS) &&
            slots_[r.slotIdx].state.load(std::memory_order_acquire) == 1) {
            slots_[r.slotIdx].key.clear();
            slots_[r.slotIdx].image.reset();
            slots_[r.slotIdx].state.store(0, std::memory_order_release);
        }
    }
    queue_.clear();
}

void ImageLoader::clearCache() {
    std::lock_guard<std::mutex> lk(queueMutex_);
    queue_.clear();
    for (size_t i = 0; i < MAX_IMAGE_SLOTS; ++i) {
        slots_[i].state.store(0, std::memory_order_release);
        slots_[i].key.clear();
        slots_[i].image.reset();
    }
}

void ImageLoader::workerLoop() {
    static int s_downloadLogCount = 0;
    while (running_) {
        Request req;
        {
            std::unique_lock<std::mutex> lk(queueMutex_);
            cv_.wait(lk, [&]() {
                return !running_ || !queue_.empty();
            });
            if (!running_) break;
            if (queue_.empty()) continue;
            req = std::move(queue_.front());
            queue_.pop_front();
        }

        // 1. Pausa de cortesía entre descargas (15 ms) para no saturar la red ni la CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        const std::string sanitizedUrl = sanitizeAmazonImageUrl(req.url);
        const std::string fetchUrl = optimizeAmazonImageUrl(sanitizedUrl, req.preset);
        int dlLog = s_downloadLogCount++;

        int origW = 0, origH = 0;
        std::vector<uint8_t> rawRgba;
        auto attempt = [&](const std::string& u) -> bool {
            auto resp = luna::net::HttpClient::get(u);
            if (dlLog < 15 || !resp.ok()) {
                luna::util::CrashLogger::log("[ImageLoader] #%d GET %s -> HTTP %d (%zu bytes)%s",
                                             dlLog + 1, u.c_str(), resp.statusCode, resp.body.size(),
                                             resp.ok() ? "" : (", err: " + resp.error).c_str());
            }
            if (!resp.ok() || resp.body.empty() || resp.body.size() > 4 * 1024 * 1024) return false;
            const uint8_t* d = reinterpret_cast<const uint8_t*>(resp.body.data());
            if (resp.body.size() > 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0) {
                luna::util::CrashLogger::log("[ImageLoader] WEBP payload received for %s (unsupported, trying JPEG variant)", u.c_str());
                return false;
            }
            return decodeImageRgba(d, resp.body.size(), rawRgba, origW, origH);
        };

        bool gotImage = attempt(fetchUrl);
        if (!gotImage && fetchUrl != sanitizedUrl) {
            gotImage = attempt(sanitizedUrl); // CDN resize failed: fall back to the original asset
        }
        if (!gotImage) {
            if (req.slotIdx >= 0 && req.slotIdx < static_cast<int>(MAX_IMAGE_SLOTS)) {
                slots_[req.slotIdx].failMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                slots_[req.slotIdx].state.store(-1, std::memory_order_release);
            }
            continue;
        }

        // Validar dimensiones máximas de seguridad
        if (origW <= 0 || origH <= 0 || origW > 4096 || origH > 4096) {
            if (req.slotIdx >= 0 && req.slotIdx < static_cast<int>(MAX_IMAGE_SLOTS)) {
                slots_[req.slotIdx].state.store(-1, std::memory_order_release);
            }
            continue;
        }

        int targetW = origW;
        int targetH = origH;
        switch (req.preset) {
            case ImagePreset::VerticalPoster:
                targetW = 200; targetH = 280; break;
            case ImagePreset::ScreenshotThumbnail:
                targetW = 270; targetH = 152; break;
            case ImagePreset::PanoramicHero:
                targetW = 1920; targetH = 540; break;
            case ImagePreset::PanoramicDetail:
                targetW = 1920; targetH = 1080; break;
            case ImagePreset::HorizontalCard16_9:
                targetW = 290; targetH = 164; break;
            case ImagePreset::SquareCategory:
                targetW = 180; targetH = 180; break;
            case ImagePreset::ProductLogo: {
                float aspect = static_cast<float>(origW) / static_cast<float>(origH);
                int maxW = 420, maxH = 160;
                if (maxW / aspect <= maxH) {
                    targetW = maxW; targetH = static_cast<int>(maxW / aspect);
                } else {
                    targetH = maxH; targetW = static_cast<int>(maxH * aspect);
                }
                break;
            }
            case ImagePreset::Raw:
            default:
                break;
        }

        std::vector<uint8_t> processedRgba;
        if (targetW == origW && targetH == origH && req.preset == ImagePreset::Raw) {
            processedRgba = std::move(rawRgba);
        } else {
            resizeAndProcessImage(rawRgba.data(), origW, origH, processedRgba, targetW, targetH, req.preset);
        }

        if (req.slotIdx >= 0 && req.slotIdx < static_cast<int>(MAX_IMAGE_SLOTS)) {
            auto dec = std::make_shared<DecodedImage>();
            dec->rgba = std::move(processedRgba);
            dec->width = targetW;
            dec->height = targetH;
            dec->ready = true;

            slots_[req.slotIdx].image = std::move(dec);
            slots_[req.slotIdx].state.store(2, std::memory_order_release);
        }
    }
}

} // namespace luna::util
