#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include "media/media_decoder.h"

namespace luna::media {

struct Mp4Sample {
    uint64_t offset = 0;
    uint32_t size = 0;
    bool isKeyframe = false;
};

class Mp4PreviewPlayer {
public:
    static Mp4PreviewPlayer& instance();

    Mp4PreviewPlayer();
    ~Mp4PreviewPlayer();

    void setVideoUrl(const std::string& url);
    void update(VideoDecoder* decoder);
    void stop();

    bool hasFrame() const { return hasDecodedFrame_.load(); }
    const uint32_t* getFrameRgba() const;
    int getWidth() const { return frameWidth_; }
    int getHeight() const { return frameHeight_; }
    const std::string& currentUrl() const { return currentUrl_; }

private:
    bool parseMp4(const uint8_t* data, size_t size);
    void reset();

    std::string currentUrl_;
    std::string downloadingUrl_;
    std::atomic<bool> isDownloading_{false};
    std::atomic<bool> isReady_{false};
    std::atomic<bool> hasDecodedFrame_{false};
    std::atomic<bool> cancelDownload_{false};

    std::thread downloadThread_;
    std::mutex downloadMutex_;

    const uint8_t* mp4DataPtr_ = nullptr;
    size_t mp4DataSize_ = 0;
    std::vector<uint8_t> spsPpsAnnexB_;
    std::vector<Mp4Sample> samples_;
    size_t currentSampleIdx_ = 0;

    std::chrono::steady_clock::time_point lastFrameTime_;
    int frameIntervalMs_ = 33; // ~30 FPS

    mutable std::mutex frameMutex_;
    int frameWidth_ = 960;
    int frameHeight_ = 540;

    std::vector<uint8_t> annexBBuffer_;
};

} // namespace luna::media
