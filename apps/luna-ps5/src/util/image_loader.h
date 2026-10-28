#pragma once
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>

namespace luna::util {

enum class ImagePreset {
    Raw,
    VerticalPoster,       // 200 x 280, 14px rounded corners
    ScreenshotThumbnail,  // 270 x 152, 10px rounded corners
    PanoramicHero,        // 1920 x 540, left & bottom dark gradient to #0E0B16
    PanoramicDetail,      // 1920 x 1080, left & bottom dark gradient to #0E0B16
    ProductLogo,          // fits up to 420 x 160 preserving aspect ratio with alpha
    HorizontalCard16_9,   // 290 x 164 px, 12px rounded corners
    SquareCategory        // 180 x 180 px, 14px rounded corners
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

static constexpr size_t MAX_IMAGE_SLOTS = 512;

struct ImageSlot {
    std::atomic<int> state{0}; // 0 = libre, 1 = reservado/descargando, 2 = listo para pintar, -1 = fallo
    std::string key;           // escrito SOLO cuando state == 1 antes de pasar a state == 2
    std::shared_ptr<const DecodedImage> image; // escrito SOLO cuando state == 1; INMUTABLE una vez que state == 2
};

class ImageLoader {
public:
    static ImageLoader& instance();

    void init();
    void shutdown();

    // Instant lock-free / acquire-read cache lookup for UI thread
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
        int slotIdx = -1;
    };

    std::mutex queueMutex_;
    std::condition_variable cv_;
    std::deque<Request> queue_;
    ImageSlot slots_[MAX_IMAGE_SLOTS];

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
