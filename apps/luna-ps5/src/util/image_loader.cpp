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

namespace luna::util {

bool decodeImageRgba(const uint8_t* data, size_t size, std::vector<uint8_t>& outRgba, int& outW, int& outH) {
    if (!data || size == 0) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char* decoded = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
    if (!decoded) return false;
    outW = w;
    outH = h;
    outRgba.assign(decoded, decoded + (static_cast<size_t>(w) * static_cast<size_t>(h) * 4));
    stbi_image_free(decoded);
    return true;
}

namespace {

inline void applyCornerMask(uint8_t* rgba, int w, int h, int r) {
    if (r <= 0 || !rgba) return;
    float rf = static_cast<float>(r);
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

            uint8_t* p = rgba + ((y * w + x) * 4);
            if (dist > rf) {
                p[3] = 0;
            } else if (dist > rf - 1.0f) {
                float factor = 1.0f - (dist - (rf - 1.0f));
                p[3] = static_cast<uint8_t>(p[3] * factor);
            }
        }
    }
}

inline void applyDarkGradient(uint8_t* rgba, int w, int h, int leftFadeW, int bottomFadeH) {
    if (!rgba) return;
    const uint8_t bgR = 0x0E;
    const uint8_t bgG = 0x0B;
    const uint8_t bgB = 0x16;

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
            uint8_t* p = rgba + ((y * w + x) * 4);

            p[0] = static_cast<uint8_t>(p[0] * alphaFactor + bgR * (1.0f - alphaFactor));
            p[1] = static_cast<uint8_t>(p[1] * alphaFactor + bgG * (1.0f - alphaFactor));
            p[2] = static_cast<uint8_t>(p[2] * alphaFactor + bgB * (1.0f - alphaFactor));
        }
    }
}

} // namespace

bool resizeAndProcessImage(const uint8_t* srcRgba, int srcW, int srcH,
                           std::vector<uint8_t>& dstRgba, int dstW, int dstH,
                           ImagePreset preset) {
    if (!srcRgba || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return false;

    dstRgba.assign(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4, 0);

    const uint8_t bgR = 0x0E;
    const uint8_t bgG = 0x0B;
    const uint8_t bgB = 0x16;

    bool isPanoramic = (preset == ImagePreset::PanoramicHero || preset == ImagePreset::PanoramicDetail);

    if (isPanoramic && srcW < srcH) {
        // IMAGEN VERTICAL (POSTER 3:4): NO estirar a 16:9 deformandola
        // Llenar todo el fondo con #0E0B16
        for (size_t i = 0; i < dstRgba.size(); i += 4) {
            dstRgba[i + 0] = bgR;
            dstRgba[i + 1] = bgG;
            dstRgba[i + 2] = bgB;
            dstRgba[i + 3] = 255;
        }

        // Escalar la imagen vertical a la derecha manteniendo su relacion de aspecto
        int fitH = static_cast<int>(dstH * 0.95f);
        int fitW = static_cast<int>(fitH * (static_cast<float>(srcW) / srcH));
        if (fitW > dstW / 2) fitW = dstW / 2;
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

            const uint8_t* row0 = srcRgba + (static_cast<size_t>(y0) * srcW * 4);
            const uint8_t* row1 = srcRgba + (static_cast<size_t>(y1) * srcW * 4);
            uint8_t* dstRow = dstRgba.data() + (static_cast<size_t>(targetY) * dstW * 4);

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

                uint8_t* out = dstRow + (targetX * 4);
                out[0] = static_cast<uint8_t>(r * alphaFade + bgR * (1.0f - alphaFade));
                out[1] = static_cast<uint8_t>(g * alphaFade + bgG * (1.0f - alphaFade));
                out[2] = static_cast<uint8_t>(b * alphaFade + bgB * (1.0f - alphaFade));
                out[3] = 255;
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
        uint8_t* dstRow = dstRgba.data() + (static_cast<size_t>(dy) * static_cast<size_t>(dstW) * 4);

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

            uint8_t* out = dstRow + (dx * 4);
            out[0] = static_cast<uint8_t>(p00[0] * w00 + p01[0] * w01 + p10[0] * w10 + p11[0] * w11);
            out[1] = static_cast<uint8_t>(p00[1] * w00 + p01[1] * w01 + p10[1] * w10 + p11[1] * w11);
            out[2] = static_cast<uint8_t>(p00[2] * w00 + p01[2] * w01 + p10[2] * w10 + p11[2] * w11);
            out[3] = static_cast<uint8_t>(p00[3] * w00 + p01[3] * w01 + p10[3] * w10 + p11[3] * w11);
        }
    }

    switch (preset) {
        case ImagePreset::VerticalPoster:
            applyCornerMask(dstRgba.data(), dstW, dstH, 14);
            break;
        case ImagePreset::ScreenshotThumbnail:
            applyCornerMask(dstRgba.data(), dstW, dstH, 10);
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

void ImageLoader::init() {
    if (running_) return;
    running_ = true;
    workerThread_ = std::thread(&ImageLoader::workerLoop, this);
}

void ImageLoader::shutdown() {
    if (!running_) return;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        running_ = false;
        queue_.clear();
    }
    cv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

static inline std::string getCacheKey(const std::string& url, ImagePreset preset) {
    return url + "@" + std::to_string(static_cast<int>(preset));
}

std::shared_ptr<const DecodedImage> ImageLoader::getImage(const std::string& url, ImagePreset preset) {
    if (url.empty()) return nullptr;
    std::string key = getCacheKey(url, preset);
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end() && it->second && it->second->ready) {
        return it->second;
    }
    // Solo permitir fallback si se pide Raw o preset identico
    if (preset == ImagePreset::Raw) {
        for (const auto& kv : cache_) {
            if (kv.first.rfind(url + "@", 0) == 0 && kv.second && kv.second->ready) {
                return kv.second;
            }
        }
    }
    return nullptr;
}

void ImageLoader::requestImage(const std::string& url, ImagePreset preset) {
    if (url.empty() || url.rfind("http", 0) != 0) return;
    std::string key = getCacheKey(url, preset);
    std::lock_guard<std::mutex> lk(mutex_);
    if (cache_.find(key) != cache_.end()) return;
    if (inFlight_[key]) return;

    inFlight_[key] = true;
    queue_.push_back(Request{url, preset});
    cv_.notify_one();
}

void ImageLoader::clearCache() {
    std::lock_guard<std::mutex> lk(mutex_);
    cache_.clear();
    inFlight_.clear();
}

void ImageLoader::workerLoop() {
    while (running_) {
        Request req;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [&]() {
                return !running_ || !queue_.empty();
            });
            if (!running_) break;
            if (queue_.empty()) continue;
            req = std::move(queue_.front());
            queue_.pop_front();
        }

        std::string reqKey = getCacheKey(req.url, req.preset);
        auto resp = luna::net::HttpClient::get(req.url);
        if (!resp.ok() || resp.body.empty()) {
            std::lock_guard<std::mutex> lk(mutex_);
            inFlight_.erase(reqKey);
            continue;
        }

        int origW = 0, origH = 0;
        std::vector<uint8_t> rawRgba;
        if (!decodeImageRgba(reinterpret_cast<const uint8_t*>(resp.body.data()),
                             resp.body.size(), rawRgba, origW, origH)) {
            std::lock_guard<std::mutex> lk(mutex_);
            inFlight_.erase(reqKey);
            continue;
        }

        int targetW = origW;
        int targetH = origH;

        switch (req.preset) {
            case ImagePreset::VerticalPoster:
                targetW = 200;
                targetH = 280;
                break;
            case ImagePreset::ScreenshotThumbnail:
                targetW = 270;
                targetH = 152;
                break;
            case ImagePreset::PanoramicHero:
                targetW = 1920;
                targetH = 540;
                break;
            case ImagePreset::PanoramicDetail:
                targetW = 1920;
                targetH = 1080;
                break;
            case ImagePreset::ProductLogo: {
                float aspect = static_cast<float>(origW) / static_cast<float>(origH);
                int maxW = 420;
                int maxH = 160;
                if (maxW / aspect <= maxH) {
                    targetW = maxW;
                    targetH = static_cast<int>(maxW / aspect);
                } else {
                    targetH = maxH;
                    targetW = static_cast<int>(maxH * aspect);
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

        {
            auto dec = std::make_shared<DecodedImage>();
            dec->rgba = std::move(processedRgba);
            dec->width = targetW;
            dec->height = targetH;
            dec->ready = true;

            std::lock_guard<std::mutex> lk(mutex_);
            cache_[reqKey] = std::move(dec);
            inFlight_.erase(reqKey);
        }
    }
}

} // namespace luna::util
