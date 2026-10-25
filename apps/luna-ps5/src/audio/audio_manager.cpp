#include "audio/audio_manager.h"
#include "util/crash_logger.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#if defined(LUNA_TARGET_PS5)
extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int userId, int type, int index, uint32_t len, uint32_t freq, uint32_t mode);
int sceAudioOutClose(int handle);
int sceAudioOutOutput(int handle, const void* ptr);
int sceAudioOutSetVolume(int handle, int channel, int* vol);
}
#endif

namespace luna::audio {

namespace {
constexpr uint32_t kGrainFrames = 256;
constexpr size_t kMaxQueuedSamples = 48000 * 2 / 4; // Max ~250ms stereo samples
constexpr size_t kTrimToSamples = 48000 * 2 / 10;   // Trim to ~100ms on overflow
} // namespace

AudioManager::AudioManager() = default;

AudioManager::~AudioManager() {
    shutdown();
}

bool AudioManager::init(int sampleRate, int channels) {
    shutdown();
    sampleRate_ = sampleRate;
    channels_ = channels;

#if defined(LUNA_TARGET_PS5)
    static bool s_audioInitialized = false;
    if (!s_audioInitialized) {
        int rc = sceAudioOutInit();
        if (rc < 0 && rc != static_cast<int>(0x8026000C) && rc != static_cast<int>(0x8026000E)) {
            luna::util::CrashLogger::log("[Audio] Error en sceAudioOutInit: 0x%08X", static_cast<unsigned>(rc));
            return false;
        }
        s_audioInitialized = true;
    }

    // Usar FloatStereo (4) igual que PSBox (audio_out_ps5.cpp), con fallback a S16Stereo (1)
    audioFormat_ = 4;
    audioHandle_ = sceAudioOutOpen(0xFF, 0, 0, kGrainFrames, static_cast<uint32_t>(sampleRate_), 4);
    if (audioHandle_ < 0) {
        audioFormat_ = 1;
        audioHandle_ = sceAudioOutOpen(0xFF, 0, 0, kGrainFrames, static_cast<uint32_t>(sampleRate_), 1);
    }
    if (audioHandle_ < 0) {
        luna::util::CrashLogger::log("[Audio] Error al abrir sceAudioOut: 0x%08X", static_cast<unsigned>(audioHandle_));
        return false;
    }
    luna::util::CrashLogger::log("[Audio] Salida de audio PS5 iniciada (handle=%d, fmt=%d, %d Hz, %d ch)",
                                 audioHandle_, audioFormat_, sampleRate_, channels_);
    running_.store(true);
    isReady_.store(true);
    audioThread_ = std::thread(&AudioManager::audioLoop, this);
#else
    std::printf("[Audio] Salida de audio simulada iniciada (%d Hz, %d ch)\n", sampleRate_, channels_);
    isReady_.store(true);
#endif

    return true;
}

void AudioManager::shutdown() {
    running_.store(false);
    isReady_.store(false);
    if (audioThread_.joinable()) {
        audioThread_.join();
    }
#if defined(LUNA_TARGET_PS5)
    if (audioHandle_ >= 0) {
        sceAudioOutClose(audioHandle_);
        audioHandle_ = -1;
        std::printf("[Audio] Salida de audio cerrada\n");
    }
#endif
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.clear();
}

void AudioManager::outputPcm(const int16_t* pcmData, size_t sampleCount) {
    if (!isReady_.load() || !pcmData || sampleCount == 0) return;

#if defined(LUNA_TARGET_PS5)
    const size_t totalSamples = sampleCount * static_cast<size_t>(channels_);
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.insert(queue_.end(), pcmData, pcmData + totalSamples);
    if (queue_.size() > kMaxQueuedSamples) {
        size_t cut = queue_.size() - kTrimToSamples;
        cut &= ~static_cast<size_t>(1); // Keep stereo alignment
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(cut));
    }
#endif
}

void AudioManager::audioLoop() {
#if defined(LUNA_TARGET_PS5)
    std::vector<int16_t> s16Grain(kGrainFrames * 2, 0);
    std::vector<float> f32Grain(kGrainFrames * 2, 0.0f);

    while (running_.load()) {
        int h = audioHandle_;
        if (h < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            const size_t needed = kGrainFrames * 2;
            if (queue_.size() >= needed) {
                for (size_t i = 0; i < needed; ++i) {
                    s16Grain[i] = queue_[i];
                }
                queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(needed));
            } else if (!queue_.empty()) {
                const size_t avail = queue_.size();
                for (size_t i = 0; i < avail; ++i) {
                    s16Grain[i] = queue_[i];
                }
                std::fill(s16Grain.begin() + static_cast<std::ptrdiff_t>(avail), s16Grain.end(), 0);
                queue_.clear();
            } else {
                std::fill(s16Grain.begin(), s16Grain.end(), 0);
            }
        }

        int rc = 0;
        if (audioFormat_ == 4) {
            constexpr float kScale = 1.0f / 32768.0f;
            for (size_t i = 0; i < kGrainFrames * 2; ++i) {
                f32Grain[i] = static_cast<float>(s16Grain[i]) * kScale;
            }
            rc = sceAudioOutOutput(h, f32Grain.data());
        } else {
            rc = sceAudioOutOutput(h, s16Grain.data());
        }

        if (rc < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
#endif
}

} // namespace luna::audio
