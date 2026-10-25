#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace luna::media {

struct DecodedVideoFrame {
    const uint8_t* y = nullptr;
    const uint8_t* u = nullptr;
    const uint8_t* v = nullptr;
    int strideY = 0;
    int strideU = 0;
    int strideV = 0;
    int width = 0;
    int height = 0;
    uint64_t pts = 0;
};

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    bool init(int maxWidth = 1920, int maxHeight = 1088);
    void shutdown();

    bool decode(const uint8_t* h264Data, size_t size, uint64_t pts, DecodedVideoFrame& outFrame);
    bool needsKeyframe() const { return needsKeyframe_; }
    bool isHardwareAccelerated() const { return hwActive_; }

private:
    bool isReady_ = false;
    bool hwActive_ = false;
    bool needsKeyframe_ = true;

    void* computeQueue_ = nullptr;
    void* decoder_ = nullptr;
    uint8_t* frameBuffer_ = nullptr;
    size_t frameBufferSize_ = 0;

    // Planos desentrelazados para NV12 -> YUV420P
    std::vector<uint8_t> planeU_;
    std::vector<uint8_t> planeV_;

    // Buffers de respaldo para modo simulación / Desktop
    std::vector<uint8_t> fallbackY_;
    std::vector<uint8_t> fallbackU_;
    std::vector<uint8_t> fallbackV_;
};

class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();

    bool init(int sampleRate = 48000, int channels = 2);
    void shutdown();

    bool decode(const uint8_t* opusData, size_t size, std::vector<int16_t>& pcmOut);

private:
    bool isReady_ = false;
    int sampleRate_ = 48000;
    int channels_ = 2;
    uint32_t frameCounter_ = 0;
    void* opusDec_ = nullptr;
};

} // namespace luna::media
