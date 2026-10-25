#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace luna::audio {

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    bool init(int sampleRate = 48000, int channels = 2);
    void shutdown();

    void outputPcm(const int16_t* pcmData, size_t sampleCount);

    bool isReady() const { return isReady_.load(); }

private:
    void audioLoop();

    int sampleRate_ = 48000;
    int channels_ = 2;
    int audioHandle_ = -1;
    int audioFormat_ = 1; // 1 = S16Stereo, 4 = FloatStereo
    std::atomic<bool> isReady_{false};
    std::atomic<bool> running_{false};
    std::mutex queueMutex_;
    std::deque<int16_t> queue_;
    std::thread audioThread_;
};

} // namespace luna::audio
