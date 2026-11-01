#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <thread>
#include "media/media_decoder.h"

namespace luna::audio {
class AudioManager;
}

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
    bool isDownloading() const { return isDownloading_.load(); }
    bool isPaused() const { return paused_.load(); }
    void setPaused(bool p);
    void togglePause();
    void setMuted(bool m);
    bool isMuted() const { return muted_.load(); }
    void setAudioManager(luna::audio::AudioManager* audioMgr) { audioManager_ = audioMgr; }
    void seekRelative(int deltaSamples);
    float getProgress() const;
    float getBufferedProgress() const;
    int getCurrentSeconds() const;
    int getTotalSeconds() const;
    size_t getDownloadedBytes() const { return downloadedBytes_.load(); }

    const uint32_t* getFrameRgba() const;
    int getWidth() const { return frameWidth_; }
    int getHeight() const { return frameHeight_; }
    const std::string& currentUrl() const { return currentUrl_; }

private:
    bool parseMp4(const uint8_t* data, size_t size);
    void reset();
    void workerLoop();
#if !defined(_WIN32)
    static void* pthreadEntry(void* arg);
    void* nativeThread_ = nullptr;
#endif

    std::string currentUrl_;
    std::string pendingUrl_;
    std::atomic<bool> hasNewRequest_{false};
    std::atomic<bool> hasRangeRequest_{false};
    std::atomic<uint64_t> pendingRangeOffset_{0};
    std::atomic<uint64_t> windowBaseOffset_{0};
    std::atomic<bool> reachedTrueEof_{false};
    std::atomic<bool> workerRunning_{false};
    std::atomic<bool> isDownloading_{false};
    std::atomic<bool> isReady_{false};
    std::atomic<bool> hasDecodedFrame_{false};
    std::atomic<bool> cancelDownload_{false};
    std::atomic<bool> needsDecoderReset_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> muted_{true};
    std::atomic<size_t> downloadedBytes_{0};
    luna::audio::AudioManager* audioManager_ = nullptr;
    void* aacDecoder_ = nullptr;

    std::thread downloadThread_;
    mutable std::mutex downloadMutex_;
    std::condition_variable cv_;

    const uint8_t* mp4DataPtr_ = nullptr;
    size_t mp4DataSize_ = 0;
    std::vector<uint8_t> spsPpsAnnexB_;
    std::vector<Mp4Sample> samples_;
    std::vector<Mp4Sample> audioSamples_;
    size_t currentSampleIdx_ = 0;
    size_t fastForwardTargetIdx_ = 0;
    size_t currentAudioIdx_ = 0;
    int videoFps_ = 30;
    int samplesPerTick_ = 1;

    std::chrono::steady_clock::time_point lastFrameTime_;
    int frameIntervalMs_ = 33; // ~30 FPS

    mutable std::mutex frameMutex_;
    int frameWidth_ = 960;
    int frameHeight_ = 540;

    std::vector<uint8_t> annexBBuffer_;
};

} // namespace luna::media
