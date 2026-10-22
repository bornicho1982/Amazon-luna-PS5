#pragma once

#include <cstdint>
#include <cstddef>

namespace luna::audio {

class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    bool init(int sampleRate = 48000, int channels = 2);
    void shutdown();

    void outputPcm(const int16_t* pcmData, size_t sampleCount);

    bool isReady() const { return isReady_; }

private:
    int sampleRate_ = 48000;
    int channels_ = 2;
    int audioHandle_ = -1;
    bool isReady_ = false;
};

} // namespace luna::audio
