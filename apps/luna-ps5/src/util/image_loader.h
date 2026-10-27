#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>
#include <cstdint>
#include <cstddef>
#include <memory>

namespace luna::util {

enum class ImagePreset {
    Raw,
    VerticalPoster,       // 200 x 280, 14px rounded corners
    ScreenshotThumbnail,  // 270 x 152, 10px rounded corners
    PanoramicHero,        // 1280 x 540, left & bottom dark gradient to #0E0B16
    PanoramicDetail,      // 1280 x 720, left & bottom dark gradient to #0E0B16
    ProductLogo           // fits up to 420 x 160 preserving aspect ratio with alpha
};

struct DecodedImage {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool ready = false;
};

bool decodeImageRgba(const uint8_t* data, size_t size, std::vector<uint8_t>& outRgba, int& outW, int& outH);

bool resizeAndProcessImage(const uint8_t* srcRgba, int srcW, int srcH,
                           std::vector<uint8_t>& dstRgba, int dstW, int dstH,
                           ImagePreset preset);

class ImageLoader {
public:
    static ImageLoader& instance();

    void init();
    void shutdown();

    // Instant non-blocking cache lookup for UI thread
    std::shared_ptr<const DecodedImage> getImage(const std::string& url, ImagePreset preset = ImagePreset::Raw);

    // Enqueues background download and pre-scale if not already cached
    void requestImage(const std::string& url, ImagePreset preset);

    void clearCache();

private:
    ImageLoader();
    ~ImageLoader();

    ImageLoader(const ImageLoader&) = delete;
    ImageLoader& operator=(const ImageLoader&) = delete;

    struct Request {
        std::string url;
        ImagePreset preset;
    };

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Request> queue_;
    std::unordered_map<std::string, std::shared_ptr<const DecodedImage>> cache_;
    std::unordered_map<std::string, bool> inFlight_;
#if defined(_WIN32)
    std::thread workerThread_;
#else
    void* nativeThread_ = nullptr;
    static void* pthreadEntry(void* arg);
#endif
    bool running_ = false;

    void workerLoop();
};

} // namespace luna::util
