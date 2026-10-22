#include "audio/audio_manager.h"
#include <cstdio>

#if defined(LUNA_TARGET_PS5)
extern "C" {
int sceAudioOutOpen(int userId, int type, int index, uint32_t len, uint32_t freq, uint32_t mode);
int sceAudioOutClose(int handle);
int sceAudioOutOutput(int handle, const void* ptr);
int sceAudioOutSetVolume(int handle, int channel, int* vol);
}
#endif

namespace luna::audio {

AudioManager::AudioManager() = default;

AudioManager::~AudioManager() {
    shutdown();
}

bool AudioManager::init(int sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_ = channels;

#if defined(LUNA_TARGET_PS5)
    // Abrir puerto de audio principal (0xFF = usuario actual, tipo 0 = principal, 512 muestras, 48000Hz, stereo)
    audioHandle_ = sceAudioOutOpen(0xFF, 0, 0, 512, sampleRate_, 0);
    if (audioHandle_ < 0) {
        std::printf("[Audio] Error al abrir sceAudioOut: 0x%08X\n", audioHandle_);
        return false;
    }
    std::printf("[Audio] Salida de audio PS5 iniciada (handle=%d, %d Hz, %d ch)\n",
                audioHandle_, sampleRate_, channels_);
#else
    std::printf("[Audio] Salida de audio simulada iniciada (%d Hz, %d ch)\n", sampleRate_, channels_);
#endif

    isReady_ = true;
    return true;
}

void AudioManager::shutdown() {
    if (isReady_) {
#if defined(LUNA_TARGET_PS5)
        if (audioHandle_ >= 0) {
            sceAudioOutClose(audioHandle_);
            audioHandle_ = -1;
        }
#endif
        isReady_ = false;
        std::printf("[Audio] Salida de audio cerrada\n");
    }
}

void AudioManager::outputPcm(const int16_t* pcmData, size_t sampleCount) {
    if (!isReady_ || !pcmData || sampleCount == 0) return;

#if defined(LUNA_TARGET_PS5)
    sceAudioOutOutput(audioHandle_, pcmData);
#endif
}

} // namespace luna::audio
