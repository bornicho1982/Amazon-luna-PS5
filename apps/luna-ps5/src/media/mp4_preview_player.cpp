#include "media/mp4_preview_player.h"
#include "audio/audio_manager.h"
#include "media/helix_aac/aacdec.h"
#include "net/http_client.h"
#include "util/crash_logger.h"
#include <fstream>
#include <algorithm>
#include <cstring>

namespace luna::media {

static uint32_t readBigU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

static uint64_t readBigU64(const uint8_t* p) {
    return (static_cast<uint64_t>(readBigU32(p)) << 32) |
            static_cast<uint64_t>(readBigU32(p + 4));
}

static uint16_t readBigU16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]);
}

#if !defined(_WIN32)
extern "C" {
    int scePthreadAttrInit(void* attr);
    int scePthreadAttrSetstacksize(void* attr, size_t stacksize);
    int scePthreadAttrDestroy(void* attr);
    int scePthreadCreate(void** thread, const void* attr, void* (*entry)(void*), void* arg, const char* name);
    int scePthreadJoin(void* thread, void** value_ptr);
}

void* Mp4PreviewPlayer::pthreadEntry(void* arg) {
    if (arg) {
        static_cast<Mp4PreviewPlayer*>(arg)->workerLoop();
    }
    return nullptr;
}
#endif

alignas(64) static uint8_t g_mp4DownloadBuffer[16 * 1024 * 1024]; // 16 MB static buffer in .bss (0 bytes heap malloc)
alignas(64) static uint32_t g_frameRgbaBuffer[960 * 540];         // 2 MB static frame buffer in .bss
alignas(64) static uint8_t g_aacDecoderArena[128 * 1024];         // 128 KB static AAC-LC decoder state in .bss
alignas(16) static int16_t g_aacPcmBuffer[4096];                  // Static PCM output buffer in .bss

Mp4PreviewPlayer& Mp4PreviewPlayer::instance() {
    static Mp4PreviewPlayer s_instance;
    return s_instance;
}

Mp4PreviewPlayer::Mp4PreviewPlayer() {
    std::fill_n(g_frameRgbaBuffer, frameWidth_ * frameHeight_, 0xFF1C1A24);
    workerRunning_.store(true);
#if defined(_WIN32)
    try {
        downloadThread_ = std::thread(&Mp4PreviewPlayer::workerLoop, this);
    } catch (...) {
        workerRunning_.store(false);
    }
#else
    alignas(16) uint8_t attr[256];
    std::memset(attr, 0, sizeof(attr));
    if (scePthreadAttrInit(attr) == 0) {
        scePthreadAttrSetstacksize(attr, 2 * 1024 * 1024);
    }
    nativeThread_ = nullptr;
    int rc = scePthreadCreate(&nativeThread_, attr, &Mp4PreviewPlayer::pthreadEntry, this, "mp4_worker");
    scePthreadAttrDestroy(attr);
    if (rc != 0 || !nativeThread_) {
        workerRunning_.store(false);
        luna::util::CrashLogger::log("[Mp4PreviewPlayer ERROR] scePthreadCreate failed: %d", rc);
    }
#endif
}

Mp4PreviewPlayer::~Mp4PreviewPlayer() {
    workerRunning_.store(false);
    cancelDownload_.store(true);
    cv_.notify_all();
#if defined(_WIN32)
    if (downloadThread_.joinable()) {
        downloadThread_.join();
    }
#else
    if (nativeThread_) {
        scePthreadJoin(nativeThread_, nullptr);
        nativeThread_ = nullptr;
    }
#endif
}

void Mp4PreviewPlayer::stop() {
    cancelDownload_.store(true);
    isReady_.store(false);
    hasDecodedFrame_.store(false);
    paused_.store(false);
    if (audioManager_) {
        audioManager_->clearQueue();
    }
    {
        std::lock_guard<std::mutex> lk(downloadMutex_);
        pendingUrl_.clear();
        hasNewRequest_.store(false);
        currentUrl_.clear();
    }
}

void Mp4PreviewPlayer::setPaused(bool p) {
    paused_.store(p);
    if (p && audioManager_) {
        audioManager_->clearQueue();
    }
}

void Mp4PreviewPlayer::togglePause() {
    bool next = !paused_.load();
    setPaused(next);
}

void Mp4PreviewPlayer::setMuted(bool m) {
    bool prev = muted_.exchange(m);
    if (!prev && m && audioManager_) {
        audioManager_->clearQueue();
    }
}

void Mp4PreviewPlayer::reset() {
    mp4DataPtr_ = nullptr;
    mp4DataSize_ = 0;
    spsPpsAnnexB_.clear();
    samples_.clear();
    audioSamples_.clear();
    currentSampleIdx_ = 0;
    fastForwardTargetIdx_ = 0;
    currentAudioIdx_ = 0;
    aacDecoder_ = nullptr;
    annexBBuffer_.clear();
    if (audioManager_) {
        audioManager_->clearQueue();
    }
}

const uint32_t* Mp4PreviewPlayer::getFrameRgba() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (!hasDecodedFrame_.load()) return nullptr;
    return g_frameRgbaBuffer;
}

void Mp4PreviewPlayer::seekRelative(int deltaSamples) {
    std::lock_guard<std::mutex> lk(downloadMutex_);
    if (!isReady_.load() || samples_.empty()) return;

    size_t dl = downloadedBytes_.load(std::memory_order_acquire);
    size_t maxPlayableSample = 0;
    for (size_t i = 0; i < samples_.size(); ++i) {
        if (samples_[i].offset + samples_[i].size <= dl) {
            maxPlayableSample = i;
        } else {
            break;
        }
    }
    if (maxPlayableSample == 0) return;

    if (deltaSamples > 0) {
        size_t baseIdx = std::max(currentSampleIdx_, fastForwardTargetIdx_);
        size_t target = std::min(baseIdx + static_cast<size_t>(deltaSamples), maxPlayableSample);
        if (target <= currentSampleIdx_) {
            return; // Already at or near end of buffered stream; never jump backwards!
        }

        // Find highest keyframe <= target
        size_t kf = target;
        while (kf > 0 && !samples_[kf].isKeyframe) {
            kf--;
        }
        // Only jump to keyframe if it is strictly ahead of currentSampleIdx_
        if (kf > currentSampleIdx_) {
            currentSampleIdx_ = kf;
            needsDecoderReset_.store(true);
        }
        fastForwardTargetIdx_ = target;
    } else if (deltaSamples < 0) {
        int rawTarget = static_cast<int>(currentSampleIdx_) + deltaSamples;
        size_t target = static_cast<size_t>(std::clamp(rawTarget, 0, static_cast<int>(maxPlayableSample)));
        size_t kf = target;
        while (kf > 0 && !samples_[kf].isKeyframe) {
            kf--;
        }
        currentSampleIdx_ = kf;
        fastForwardTargetIdx_ = target;
        needsDecoderReset_.store(true);
    }

    if (!audioSamples_.empty() && !samples_.empty()) {
        currentAudioIdx_ = (fastForwardTargetIdx_ * audioSamples_.size()) / samples_.size();
    }
    if (aacDecoder_) {
        AACFlushCodec(aacDecoder_);
    }
    if (audioManager_) {
        audioManager_->clearQueue();
    }
}

float Mp4PreviewPlayer::getProgress() const {
    if (!isReady_.load() || samples_.empty()) return 0.0f;
    return std::clamp(static_cast<float>(currentSampleIdx_) / static_cast<float>(samples_.size()), 0.0f, 1.0f);
}

float Mp4PreviewPlayer::getBufferedProgress() const {
    if (!isReady_.load() || samples_.empty()) return 0.0f;
    size_t dl = downloadedBytes_.load(std::memory_order_acquire);
    size_t playableSamples = samples_.size();
    for (size_t i = 0; i < samples_.size(); ++i) {
        if (samples_[i].offset + samples_[i].size > dl) {
            playableSamples = i;
            break;
        }
    }
    return std::clamp(static_cast<float>(playableSamples) / static_cast<float>(samples_.size()), 0.0f, 1.0f);
}

int Mp4PreviewPlayer::getCurrentSeconds() const {
    if (!isReady_.load()) return 0;
    return static_cast<int>(currentSampleIdx_ / 30);
}

int Mp4PreviewPlayer::getTotalSeconds() const {
    if (!isReady_.load() || samples_.empty()) return 0;
    return std::max(1, static_cast<int>(samples_.size() / 30));
}

void Mp4PreviewPlayer::setVideoUrl(const std::string& url) {
    if (url.empty() || (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)) {
        stop();
        return;
    }

    {
        std::lock_guard<std::mutex> lk(downloadMutex_);
        if (url == currentUrl_) {
            return;
        }
        currentUrl_ = url;
        pendingUrl_ = url;
        hasNewRequest_.store(true);
    }

    cancelDownload_.store(true);
    isReady_.store(false);
    hasDecodedFrame_.store(false);
    paused_.store(false);
    cv_.notify_one();
}

void Mp4PreviewPlayer::workerLoop() {
    while (workerRunning_.load()) {
        std::string urlToFetch;
        {
            std::unique_lock<std::mutex> lk(downloadMutex_);
            cv_.wait(lk, [this]() {
                return !workerRunning_.load() || hasNewRequest_.load();
            });
            if (!workerRunning_.load()) break;
            urlToFetch = pendingUrl_;
            hasNewRequest_.store(false);
        }

        if (urlToFetch.empty()) continue;

        {
            std::lock_guard<std::mutex> lk(downloadMutex_);
            reset();
        }
        downloadedBytes_.store(0, std::memory_order_release);
        cancelDownload_.store(false);
        isDownloading_.store(true);

        size_t finalSize = 0;
        luna::util::CrashLogger::log("[Mp4PreviewPlayer] Streaming progressive HTTP RAM: %s", urlToFetch.c_str());

        auto resp = luna::net::HttpClient::getIntoBuffer(urlToFetch,
                                                        g_mp4DownloadBuffer,
                                                        sizeof(g_mp4DownloadBuffer),
                                                        finalSize,
                                                        &cancelDownload_,
                                                        {},
                                                        &downloadedBytes_);

        isDownloading_.store(false);

        if (cancelDownload_.load() || hasNewRequest_.load()) {
            continue;
        }

        if (resp.ok() && finalSize > 1024) {
            std::lock_guard<std::mutex> lk(downloadMutex_);
            mp4DataSize_ = finalSize;
            downloadedBytes_.store(finalSize, std::memory_order_release);
            if (!isReady_.load()) {
                if (parseMp4(g_mp4DownloadBuffer, finalSize)) {
                    mp4DataPtr_ = g_mp4DownloadBuffer;
                    currentSampleIdx_ = 0;
                    lastFrameTime_ = std::chrono::steady_clock::now();
                    needsDecoderReset_.store(true);
                    isReady_.store(true);
                    luna::util::CrashLogger::log("[Mp4PreviewPlayer] Ready on completion (%zu bytes, %zu samples)", finalSize, samples_.size());
                }
            } else {
                luna::util::CrashLogger::log("[Mp4PreviewPlayer] Progressive stream finished (%zu bytes buffered)", finalSize);
            }
        } else {
            luna::util::CrashLogger::log("[Mp4PreviewPlayer] Failed to stream video (%d: %s)",
                                         resp.statusCode, resp.error.c_str());
        }
    }
}

bool Mp4PreviewPlayer::parseMp4(const uint8_t* data, size_t size) {
    if (!data || size < 64) return false;

    // Helper lambda to find an atom within a range
    auto findBox = [data](const char target[4], size_t start, size_t end,
                          size_t& outStart, size_t& outSize, size_t& outHdr) -> bool {
        size_t cur = start;
        while (cur + 8 <= end) {
            uint32_t sz = readBigU32(data + cur);
            size_t hdr = 8;
            size_t boxSize = sz;
            if (sz == 1) {
                if (cur + 16 > end) return false;
                boxSize = static_cast<size_t>(readBigU64(data + cur + 8));
                hdr = 16;
            } else if (sz == 0) {
                boxSize = end - cur;
            }
            if (boxSize < hdr) return false;

            if (std::memcmp(data + cur + 4, target, 4) == 0) {
                outStart = cur;
                outSize = boxSize;
                outHdr = hdr;
                return true;
            }
            cur += boxSize;
        }
        return false;
    };

    size_t moovStart = 0, moovSize = 0, moovHdr = 0;
    if (!findBox("moov", 0, size, moovStart, moovSize, moovHdr)) {
        return false;
    }

    // Find video and audio traks inside moov
    size_t moovEnd = moovStart + moovSize;
    size_t curTrak = moovStart + moovHdr;
    size_t videoTrakStart = 0, videoTrakSize = 0, videoTrakHdr = 0;
    size_t audioTrakStart = 0, audioTrakSize = 0, audioTrakHdr = 0;
    bool foundVideoTrak = false;
    bool foundAudioTrak = false;

    while (curTrak + 8 <= moovEnd) {
        size_t tkStart = 0, tkSize = 0, tkHdr = 0;
        if (!findBox("trak", curTrak, moovEnd, tkStart, tkSize, tkHdr)) {
            break;
        }
        size_t mdiaStart = 0, mdiaSize = 0, mdiaHdr = 0;
        if (findBox("mdia", tkStart + tkHdr, tkStart + tkSize, mdiaStart, mdiaSize, mdiaHdr)) {
            size_t hdlrStart = 0, hdlrSize = 0, hdlrHdr = 0;
            if (findBox("hdlr", mdiaStart + mdiaHdr, mdiaStart + mdiaSize, hdlrStart, hdlrSize, hdlrHdr)) {
                if (hdlrSize >= hdlrHdr + 12) {
                    if (std::memcmp(data + hdlrStart + hdlrHdr + 8, "vide", 4) == 0 && !foundVideoTrak) {
                        videoTrakStart = tkStart;
                        videoTrakSize = tkSize;
                        videoTrakHdr = tkHdr;
                        foundVideoTrak = true;
                    } else if (std::memcmp(data + hdlrStart + hdlrHdr + 8, "soun", 4) == 0 && !foundAudioTrak) {
                        audioTrakStart = tkStart;
                        audioTrakSize = tkSize;
                        audioTrakHdr = tkHdr;
                        foundAudioTrak = true;
                    }
                }
            }
        }
        curTrak = tkStart + tkSize;
    }

    if (!foundVideoTrak) return false;

    size_t mdiaStart = 0, mdiaSize = 0, mdiaHdr = 0;
    if (!findBox("mdia", videoTrakStart + videoTrakHdr, videoTrakStart + videoTrakSize, mdiaStart, mdiaSize, mdiaHdr)) return false;

    size_t minfStart = 0, minfSize = 0, minfHdr = 0;
    if (!findBox("minf", mdiaStart + mdiaHdr, mdiaStart + mdiaSize, minfStart, minfSize, minfHdr)) return false;

    size_t stblStart = 0, stblSize = 0, stblHdr = 0;
    if (!findBox("stbl", minfStart + minfHdr, minfStart + minfSize, stblStart, stblSize, stblHdr)) return false;

    // Search for avcC atom inside stbl
    spsPpsAnnexB_.clear();
    for (size_t p = stblStart; p + 8 < stblStart + stblSize; ++p) {
        if (std::memcmp(data + p, "avcC", 4) == 0) {
            uint32_t avccSz = readBigU32(data + p - 4);
            if (avccSz >= 11 && p + avccSz - 4 <= size) {
                const uint8_t* avcc = data + p + 4;
                uint8_t numSps = avcc[5] & 0x1F;
                size_t off = 6;
                for (uint8_t i = 0; i < numSps && off + 2 <= avccSz; ++i) {
                    uint16_t spsLen = readBigU16(avcc + off);
                    off += 2;
                    if (off + spsLen <= avccSz) {
                        spsPpsAnnexB_.push_back(0);
                        spsPpsAnnexB_.push_back(0);
                        spsPpsAnnexB_.push_back(0);
                        spsPpsAnnexB_.push_back(1);
                        spsPpsAnnexB_.insert(spsPpsAnnexB_.end(), avcc + off, avcc + off + spsLen);
                        off += spsLen;
                    }
                }
                if (off < avccSz) {
                    uint8_t numPps = avcc[off++];
                    for (uint8_t i = 0; i < numPps && off + 2 <= avccSz; ++i) {
                        uint16_t ppsLen = readBigU16(avcc + off);
                        off += 2;
                        if (off + ppsLen <= avccSz) {
                            spsPpsAnnexB_.push_back(0);
                            spsPpsAnnexB_.push_back(0);
                            spsPpsAnnexB_.push_back(0);
                            spsPpsAnnexB_.push_back(1);
                            spsPpsAnnexB_.insert(spsPpsAnnexB_.end(), avcc + off, avcc + off + ppsLen);
                            off += ppsLen;
                        }
                    }
                }
                break;
            }
        }
    }

    if (spsPpsAnnexB_.empty()) {
        luna::util::CrashLogger::log("[Mp4PreviewPlayer] Warning: No SPS/PPS found in avcC");
    }

    // Parse stsz (sample sizes)
    size_t stszStart = 0, stszSize = 0, stszHdr = 0;
    if (!findBox("stsz", stblStart + stblHdr, stblStart + stblSize, stszStart, stszSize, stszHdr)) return false;

    const uint8_t* stszData = data + stszStart + stszHdr;
    uint32_t defSize = readBigU32(stszData + 4);
    uint32_t sampleCount = readBigU32(stszData + 8);
    if (sampleCount == 0 || sampleCount > 50000) return false;

    std::vector<uint32_t> sampleSizes(sampleCount);
    if (defSize != 0) {
        std::fill(sampleSizes.begin(), sampleSizes.end(), defSize);
    } else {
        if (stszSize < stszHdr + 12 + sampleCount * 4) return false;
        for (uint32_t i = 0; i < sampleCount; ++i) {
            sampleSizes[i] = readBigU32(stszData + 12 + i * 4);
        }
    }

    // Parse stco or co64 (chunk offsets)
    std::vector<uint64_t> chunkOffsets;
    size_t stcoStart = 0, stcoSize = 0, stcoHdr = 0;
    if (findBox("stco", stblStart + stblHdr, stblStart + stblSize, stcoStart, stcoSize, stcoHdr)) {
        const uint8_t* stcoData = data + stcoStart + stcoHdr;
        uint32_t chunkCount = readBigU32(stcoData + 4);
        chunkOffsets.resize(chunkCount);
        for (uint32_t i = 0; i < chunkCount; ++i) {
            chunkOffsets[i] = readBigU32(stcoData + 8 + i * 4);
        }
    } else {
        size_t co64Start = 0, co64Size = 0, co64Hdr = 0;
        if (findBox("co64", stblStart + stblHdr, stblStart + stblSize, co64Start, co64Size, co64Hdr)) {
            const uint8_t* co64Data = data + co64Start + co64Hdr;
            uint32_t chunkCount = readBigU32(co64Data + 4);
            chunkOffsets.resize(chunkCount);
            for (uint32_t i = 0; i < chunkCount; ++i) {
                chunkOffsets[i] = readBigU64(co64Data + 8 + i * 8);
            }
        } else {
            return false;
        }
    }

    // Parse stsc (samples per chunk)
    size_t stscStart = 0, stscSize = 0, stscHdr = 0;
    if (!findBox("stsc", stblStart + stblHdr, stblStart + stblSize, stscStart, stscSize, stscHdr)) return false;

    const uint8_t* stscData = data + stscStart + stscHdr;
    uint32_t stscCount = readBigU32(stscData + 4);
    struct StscEntry { uint32_t firstChunk; uint32_t samplesPerChunk; };
    std::vector<StscEntry> stscEntries(stscCount);
    for (uint32_t i = 0; i < stscCount; ++i) {
        stscEntries[i].firstChunk = readBigU32(stscData + 8 + i * 12);
        stscEntries[i].samplesPerChunk = readBigU32(stscData + 8 + i * 12 + 4);
    }

    // Parse stss (sync samples / keyframes)
    std::vector<bool> isKeyframeTable(sampleCount, false);
    size_t stssStart = 0, stssSize = 0, stssHdr = 0;
    if (findBox("stss", stblStart + stblHdr, stblStart + stblSize, stssStart, stssSize, stssHdr)) {
        const uint8_t* stssData = data + stssStart + stssHdr;
        uint32_t syncCount = readBigU32(stssData + 4);
        for (uint32_t i = 0; i < syncCount; ++i) {
            uint32_t idx = readBigU32(stssData + 8 + i * 4);
            if (idx > 0 && idx <= sampleCount) {
                isKeyframeTable[idx - 1] = true;
            }
        }
    } else {
        std::fill(isKeyframeTable.begin(), isKeyframeTable.end(), true);
    }
    isKeyframeTable[0] = true; // Always treat sample 0 as keyframe

    // Map each sample to offset and size
    samples_.clear();
    samples_.reserve(sampleCount);
    size_t curSample = 0;

    for (size_t chunkIdx = 0; chunkIdx < chunkOffsets.size(); ++chunkIdx) {
        uint32_t chunkNum = static_cast<uint32_t>(chunkIdx + 1);
        uint32_t spc = 1;
        for (const auto& entry : stscEntries) {
            if (chunkNum >= entry.firstChunk) {
                spc = entry.samplesPerChunk;
            } else {
                break;
            }
        }
        uint64_t chunkOffset = chunkOffsets[chunkIdx];
        for (uint32_t s = 0; s < spc && curSample < sampleCount; ++s) {
            Mp4Sample smp{};
            smp.offset = chunkOffset;
            smp.size = sampleSizes[curSample];
            smp.isKeyframe = isKeyframeTable[curSample];
            samples_.push_back(smp);

            chunkOffset += smp.size;
            curSample++;
        }
    }

    // Parse audio trak sample table (AAC-LC 48kHz stereo) if present
    audioSamples_.clear();
    currentAudioIdx_ = 0;
    aacDecoder_ = nullptr;
    if (foundAudioTrak) {
        size_t aMdiaStart = 0, aMdiaSize = 0, aMdiaHdr = 0;
        size_t aMinfStart = 0, aMinfSize = 0, aMinfHdr = 0;
        size_t aStblStart = 0, aStblSize = 0, aStblHdr = 0;
        if (findBox("mdia", audioTrakStart + audioTrakHdr, audioTrakStart + audioTrakSize, aMdiaStart, aMdiaSize, aMdiaHdr) &&
            findBox("minf", aMdiaStart + aMdiaHdr, aMdiaStart + aMdiaSize, aMinfStart, aMinfSize, aMinfHdr) &&
            findBox("stbl", aMinfStart + aMinfHdr, aMinfStart + aMinfSize, aStblStart, aStblSize, aStblHdr)) {

            size_t aStszStart = 0, aStszSize = 0, aStszHdr = 0;
            size_t aStscStart = 0, aStscSize = 0, aStscHdr = 0;
            if (findBox("stsz", aStblStart + aStblHdr, aStblStart + aStblSize, aStszStart, aStszSize, aStszHdr) &&
                findBox("stsc", aStblStart + aStblHdr, aStblStart + aStblSize, aStscStart, aStscSize, aStscHdr)) {

                const uint8_t* aStszData = data + aStszStart + aStszHdr;
                uint32_t aDefSize = readBigU32(aStszData + 4);
                uint32_t aSampleCount = readBigU32(aStszData + 8);
                if (aSampleCount > 0 && aSampleCount <= 100000) {
                    std::vector<uint32_t> aSampleSizes(aSampleCount);
                    bool stszOk = true;
                    if (aDefSize != 0) {
                        std::fill(aSampleSizes.begin(), aSampleSizes.end(), aDefSize);
                    } else if (aStszSize >= aStszHdr + 12 + aSampleCount * 4) {
                        for (uint32_t i = 0; i < aSampleCount; ++i) {
                            aSampleSizes[i] = readBigU32(aStszData + 12 + i * 4);
                        }
                    } else {
                        stszOk = false;
                    }

                    std::vector<uint64_t> aChunkOffsets;
                    size_t aStcoStart = 0, aStcoSize = 0, aStcoHdr = 0;
                    if (stszOk && findBox("stco", aStblStart + aStblHdr, aStblStart + aStblSize, aStcoStart, aStcoSize, aStcoHdr)) {
                        const uint8_t* aStcoData = data + aStcoStart + aStcoHdr;
                        uint32_t aChunkCount = readBigU32(aStcoData + 4);
                        aChunkOffsets.resize(aChunkCount);
                        for (uint32_t i = 0; i < aChunkCount; ++i) {
                            aChunkOffsets[i] = readBigU32(aStcoData + 8 + i * 4);
                        }
                    } else if (stszOk) {
                        size_t aCo64Start = 0, aCo64Size = 0, aCo64Hdr = 0;
                        if (findBox("co64", aStblStart + aStblHdr, aStblStart + aStblSize, aCo64Start, aCo64Size, aCo64Hdr)) {
                            const uint8_t* aCo64Data = data + aCo64Start + aCo64Hdr;
                            uint32_t aChunkCount = readBigU32(aCo64Data + 4);
                            aChunkOffsets.resize(aChunkCount);
                            for (uint32_t i = 0; i < aChunkCount; ++i) {
                                aChunkOffsets[i] = readBigU64(aCo64Data + 8 + i * 8);
                            }
                        }
                    }

                    if (!aChunkOffsets.empty()) {
                        const uint8_t* aStscData = data + aStscStart + aStscHdr;
                        uint32_t aStscCount = readBigU32(aStscData + 4);
                        std::vector<StscEntry> aStscEntries(aStscCount);
                        for (uint32_t i = 0; i < aStscCount; ++i) {
                            aStscEntries[i].firstChunk = readBigU32(aStscData + 8 + i * 12);
                            aStscEntries[i].samplesPerChunk = readBigU32(aStscData + 8 + i * 12 + 4);
                        }

                        audioSamples_.reserve(aSampleCount);
                        size_t curASample = 0;
                        for (size_t chunkIdx = 0; chunkIdx < aChunkOffsets.size(); ++chunkIdx) {
                            uint32_t chunkNum = static_cast<uint32_t>(chunkIdx + 1);
                            uint32_t spc = 1;
                            for (const auto& entry : aStscEntries) {
                                if (chunkNum >= entry.firstChunk) {
                                    spc = entry.samplesPerChunk;
                                } else {
                                    break;
                                }
                            }
                            uint64_t chunkOff = aChunkOffsets[chunkIdx];
                            for (uint32_t s = 0; s < spc && curASample < aSampleCount; ++s) {
                                Mp4Sample asmp{};
                                asmp.offset = chunkOff;
                                asmp.size = aSampleSizes[curASample];
                                asmp.isKeyframe = true;
                                audioSamples_.push_back(asmp);
                                chunkOff += asmp.size;
                                curASample++;
                            }
                        }

                        if (!audioSamples_.empty()) {
                            std::memset(g_aacDecoderArena, 0, sizeof(g_aacDecoderArena));
                            aacDecoder_ = AACInitDecoderPre(g_aacDecoderArena, sizeof(g_aacDecoderArena));
                            if (aacDecoder_) {
                                AACFrameInfo aacInfo{};
                                aacInfo.nChans = 2;
                                aacInfo.sampRateCore = 48000;
                                aacInfo.profile = AAC_PROFILE_LC;
                                AACSetRawBlockParams(aacDecoder_, 0, &aacInfo);
                            }
                        }
                    }
                }
            }
        }
    }

    return !samples_.empty();
}

void Mp4PreviewPlayer::update(VideoDecoder* decoder) {
    if (!decoder) return;

    // Progressive start: as soon as 512 KB of Fast-Start MP4 is in RAM, parse moov immediately
    if (!isReady_.load()) {
        size_t dl = downloadedBytes_.load(std::memory_order_acquire);
        if (dl >= 512 * 1024 && isDownloading_.load()) {
            std::lock_guard<std::mutex> lk(downloadMutex_);
            if (!isReady_.load() && !cancelDownload_.load()) {
                if (parseMp4(g_mp4DownloadBuffer, dl)) {
                    mp4DataPtr_ = g_mp4DownloadBuffer;
                    mp4DataSize_ = dl;
                    currentSampleIdx_ = 0;
                    fastForwardTargetIdx_ = 0;
                    currentAudioIdx_ = 0;
                    lastFrameTime_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(frameIntervalMs_);
                    needsDecoderReset_.store(true);
                    isReady_.store(true);
                    luna::util::CrashLogger::log("[Mp4PreviewPlayer] Progressive start! Parsed %zu video + %zu audio samples at %zu KB",
                                                 samples_.size(), audioSamples_.size(), dl / 1024);
                }
            }
        }
        if (!isReady_.load()) return;
    }

    if (paused_.load()) return;

    std::lock_guard<std::mutex> lk(downloadMutex_);
    if (!isReady_.load() || samples_.empty() || !mp4DataPtr_) return;

    const bool isFastForwarding = (currentSampleIdx_ < fastForwardTargetIdx_);
    auto now = std::chrono::steady_clock::now();
    if (!isFastForwarding) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime_).count();
        if (elapsedMs < frameIntervalMs_) {
            return;
        }
    }
    lastFrameTime_ = now;

    if (needsDecoderReset_.exchange(false)) {
        decoder->resetStreamState();
    }

    size_t availableBytes = downloadedBytes_.load(std::memory_order_acquire);
    if (availableBytes == 0) return;

    int maxSteps = isFastForwarding ? 35 : (hasDecodedFrame_.load() ? 1 : 4);
    for (int step = 0; step < maxSteps; ++step) {
        if (currentSampleIdx_ >= samples_.size()) {
            currentSampleIdx_ = 0;
            fastForwardTargetIdx_ = 0;
            currentAudioIdx_ = 0;
        }

        const auto& sample = samples_[currentSampleIdx_];
        if (sample.offset + sample.size > availableBytes) {
            if (isDownloading_.load()) {
                // Buffering: wait for HTTP stream to deliver this sample
                fastForwardTargetIdx_ = currentSampleIdx_;
                return;
            } else {
                // Download reached 16 MB cap or EOF: wrap immediately to beginning of trailer
                currentSampleIdx_ = 0;
                fastForwardTargetIdx_ = 0;
                currentAudioIdx_ = 0;
                if (samples_[0].offset + samples_[0].size > availableBytes) return;
            }
        }

        const auto& curSample = samples_[currentSampleIdx_];

        // Build clean Annex-B packet (stripping NAL 9 Access Unit Delimiter and NAL 12 Filler padding)
        annexBBuffer_.clear();
        const bool prependHeader = (currentSampleIdx_ == 0 || curSample.isKeyframe);
        if (prependHeader && !spsPpsAnnexB_.empty()) {
            annexBBuffer_.insert(annexBBuffer_.end(), spsPpsAnnexB_.begin(), spsPpsAnnexB_.end());
        }

        const uint8_t* sampleBytes = mp4DataPtr_ + curSample.offset;
        size_t cur = 0;
        while (cur + 4 <= curSample.size) {
            uint32_t nalSize = readBigU32(sampleBytes + cur);
            cur += 4;
            if (nalSize == 0 || cur + nalSize > curSample.size) break;

            uint8_t nalType = sampleBytes[cur] & 0x1F;
            if (nalType != 9 && nalType != 12) {
                annexBBuffer_.push_back(0);
                annexBBuffer_.push_back(0);
                annexBBuffer_.push_back(0);
                annexBBuffer_.push_back(1);
                annexBBuffer_.insert(annexBBuffer_.end(), sampleBytes + cur, sampleBytes + cur + nalSize);
            }
            cur += nalSize;
        }

        currentSampleIdx_ = (currentSampleIdx_ + 1) % samples_.size();
        const bool reachedTarget = (currentSampleIdx_ >= fastForwardTargetIdx_);
        if (reachedTarget) {
            fastForwardTargetIdx_ = 0;
        }

        if (!annexBBuffer_.empty()) {
            DecodedVideoFrame outFrame{};
            uint64_t pts = currentSampleIdx_ * 33333; // 30 FPS PTS
            if (decoder->decode(annexBBuffer_.data(), annexBBuffer_.size(), pts, outFrame, reachedTarget)) {
                if (reachedTarget && outFrame.width > 0 && outFrame.height > 0 && outFrame.y && outFrame.u && outFrame.v) {
                    const int dw = frameWidth_;
                    const int dh = frameHeight_;
                    const int sw = outFrame.width;
                    const int sh = outFrame.height;

                    std::lock_guard<std::mutex> lock(frameMutex_);
                    for (int y = 0; y < dh; ++y) {
                        int srcY = (y * sh) / dh;
                        int uvY = srcY / 2;
                        const uint8_t* yRow = outFrame.y + srcY * outFrame.strideY;
                        const uint8_t* uRow = outFrame.u + uvY * outFrame.strideU;
                        const uint8_t* vRow = outFrame.v + uvY * outFrame.strideV;
                        uint32_t* dstRow = &g_frameRgbaBuffer[y * dw];

                        for (int x = 0; x < dw; ++x) {
                            int srcX = (x * sw) / dw;
                            int uvX = srcX / 2;
                            int Y = yRow[srcX];
                            int U = uRow[uvX];
                            int V = vRow[uvX];

                            int c = Y - 16;
                            int d = U - 128;
                            int e = V - 128;

                            int r = (298 * c + 409 * e + 128) >> 8;
                            int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                            int b = (298 * c + 516 * d + 128) >> 8;

                            if (r < 0) r = 0; else if (r > 255) r = 255;
                            if (g < 0) g = 0; else if (g > 255) g = 255;
                            if (b < 0) b = 0; else if (b > 255) b = 255;

                            // Little endian word for canvas_: 0xFF000000 | (B << 16) | (G << 8) | R
                            dstRow[x] = 0xFF000000U | (static_cast<uint32_t>(b) << 16) |
                                                       (static_cast<uint32_t>(g) << 8)  |
                                                        static_cast<uint32_t>(r);
                        }
                    }
                    hasDecodedFrame_.store(true);
                    break;
                }
            }
        }
    }

    // Decode synchronized AAC-LC audio frames when unmuted (Fullscreen mode)
    if (!audioSamples_.empty() && !samples_.empty() && aacDecoder_ && currentSampleIdx_ >= fastForwardTargetIdx_) {
        size_t targetAudioIdx = (currentSampleIdx_ * audioSamples_.size()) / samples_.size();
        if (muted_.load() || !audioManager_) {
            currentAudioIdx_ = targetAudioIdx;
        } else {
            if (currentAudioIdx_ + 8 < targetAudioIdx || currentAudioIdx_ > targetAudioIdx + 8) {
                currentAudioIdx_ = targetAudioIdx;
                AACFlushCodec(aacDecoder_);
            }
            int decodedAudioFrames = 0;
            while (currentAudioIdx_ <= targetAudioIdx + 1 && currentAudioIdx_ < audioSamples_.size() && decodedAudioFrames < 3) {
                const auto& asmp = audioSamples_[currentAudioIdx_];
                if (asmp.offset + asmp.size > availableBytes) {
                    break;
                }
                unsigned char* inptr = const_cast<unsigned char*>(mp4DataPtr_ + asmp.offset);
                int bytesLeft = static_cast<int>(asmp.size);
                int err = AACDecode(aacDecoder_, &inptr, &bytesLeft, g_aacPcmBuffer);
                if (err == 0) {
                    AACFrameInfo aacInfo{};
                    AACGetLastFrameInfo(aacDecoder_, &aacInfo);
                    if (aacInfo.outputSamps > 0 && aacInfo.nChans > 0) {
                        audioManager_->outputPcm(g_aacPcmBuffer, static_cast<size_t>(aacInfo.outputSamps / aacInfo.nChans));
                    }
                }
                currentAudioIdx_++;
                decodedAudioFrames++;
            }
        }
    }
}

} // namespace luna::media
