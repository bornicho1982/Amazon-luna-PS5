"""Patch ImageLoader: URL sanitize/optimize (webp->jpg, server-side UX resize), slot eviction, retry, pending clear."""
H = 'apps/luna-ps5/src/util/image_loader.h'
C = 'apps/luna-ps5/src/util/image_loader.cpp'


def load(p):
    # latin-1 keeps any legacy non-UTF8 bytes untouched
    return open(p, 'rb').read().decode('latin-1').replace('\r\r\n', '\n').replace('\r\n', '\n')


def save(p, s):
    open(p, 'wb').write(s.replace('\n', '\r\n').encode('latin-1'))


def rep(s, old, new):
    assert old in s, 'NOT FOUND: ' + old[:70]
    return s.replace(old, new, 1)


def between(s, start, end, new):
    a = s.index(start)
    b = s.index(end, a)
    return s[:a] + new + s[b:]


h = load(H)
h = rep(h, 'static constexpr size_t MAX_IMAGE_SLOTS = 512;', '''// Normalizes Amazon image URLs: .webp -> .jpg and _FMwebp_ -> _FMjpg_ (stb_image cannot decode WebP)
std::string sanitizeAmazonImageUrl(const std::string& url);

// Asks the Amazon image CDN for a right-sized JPEG (._UX<w>_) instead of multi-MB PNGs. Logos/Raw are untouched.
std::string optimizeAmazonImageUrl(const std::string& url, ImagePreset preset);

static constexpr size_t MAX_IMAGE_SLOTS = 512;''')
h = rep(h, '''    std::shared_ptr<const DecodedImage> image; // escrito''', '''    uint64_t lastUse = 0;      // LRU tick (guarded by queueMutex_)
    uint64_t failMs = 0;       // time of last failure, used for retry back-off
    std::shared_ptr<const DecodedImage> image; // escrito''')
h = rep(h, '''    void clearCache();
''', '''    void clearCache();

    // Drops every queued (not yet started) download so the screen the user is looking at gets bandwidth first.
    // Dropped requests are simply re-issued by the draw code if their image is still needed.
    void prioritizeOrClearPending();
''')
h = rep(h, '    bool running_ = false;\n', '    bool running_ = false;\n    uint64_t tick_ = 0;\n')
save(H, h)

c = load(C)
c = rep(c, '#include <cstring>\n', '#include <cstring>\n#include <chrono>\n')

helpers = '''
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
'''
c = rep(c, '\nnamespace {\n', helpers)

# getImage / requestImage with LRU eviction + retry
new_get_request = r'''std::shared_ptr<const DecodedImage> ImageLoader::getImage(const std::string& urlIn, ImagePreset preset) {
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

'''
c = between(c, 'std::shared_ptr<const DecodedImage> ImageLoader::getImage(', 'void ImageLoader::clearCache()', new_get_request)

# worker fetch+decode with webp detection, optimized url and fallback
worker_new = r'''        const std::string sanitizedUrl = sanitizeAmazonImageUrl(req.url);
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

'''
c = between(c, '        auto resp = luna::net::HttpClient::get(req.url);', '        // Validar dimensiones m', worker_new)
c = rep(c, 'std::this_thread::sleep_for(std::chrono::milliseconds(15));', 'std::this_thread::sleep_for(std::chrono::milliseconds(2));')
save(C, c)
print('image loader patched OK')
