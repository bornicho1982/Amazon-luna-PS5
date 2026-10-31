#include "media/mp4_preview_player.h"
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

alignas(64) static uint8_t g_mp4DownloadBuffer[20 * 1024 * 1024]; // 20 MB static buffer in .bss (0 bytes heap malloc)
alignas(64) static uint32_t g_frameRgbaBuffer[960 * 540];         // 2 MB static frame buffer in .bss

Mp4PreviewPlayer& Mp4PreviewPlayer::instance() {
    static Mp4PreviewPlayer s_instance;
    return s_instance;
}

Mp4PreviewPlayer::Mp4PreviewPlayer() {
    std::fill_n(g_frameRgbaBuffer, frameWidth_ * frameHeight_, 0xFF1C1A24);
}

Mp4PreviewPlayer::~Mp4PreviewPlayer() {
    stop();
}

void Mp4PreviewPlayer::stop() {
    cancelDownload_.store(true);
    isReady_.store(false);
    hasDecodedFrame_.store(false);

    if (downloadThread_.joinable()) {
        downloadThread_.join();
    }

    reset();
    currentUrl_.clear();
}

void Mp4PreviewPlayer::reset() {
    mp4DataPtr_ = nullptr;
    mp4DataSize_ = 0;
    spsPpsAnnexB_.clear();
    samples_.clear();
    currentSampleIdx_ = 0;
    annexBBuffer_.clear();
}

const uint32_t* Mp4PreviewPlayer::getFrameRgba() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (!hasDecodedFrame_.load()) return nullptr;
    return g_frameRgbaBuffer;
}

void Mp4PreviewPlayer::setVideoUrl(const std::string& url) {
    if (url.empty() || (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)) {
        stop();
        return;
    }

    if (url == currentUrl_) {
        return;
    }

    // Stop current playback cleanly and signal abort on active download
    stop();

    currentUrl_ = url;
    downloadingUrl_ = url;
    cancelDownload_.store(false);
    isDownloading_.store(true);

    downloadThread_ = std::thread([this, url]() {
        size_t downloadedSize = 0;
        luna::util::CrashLogger::log("[Mp4PreviewPlayer] Streaming trailer HTTP RAM: %s", url.c_str());

        auto resp = luna::net::HttpClient::getIntoBuffer(url,
                                                        g_mp4DownloadBuffer,
                                                        sizeof(g_mp4DownloadBuffer),
                                                        downloadedSize,
                                                        &cancelDownload_);

        isDownloading_.store(false);

        if (cancelDownload_.load()) {
            luna::util::CrashLogger::log("[Mp4PreviewPlayer] Video download aborted by user navigation");
            return;
        }

        if (resp.ok() && downloadedSize > 1024) {
            luna::util::CrashLogger::log("[Mp4PreviewPlayer] Downloaded %zu bytes into RAM (.bss)", downloadedSize);
            if (parseMp4(g_mp4DownloadBuffer, downloadedSize)) {
                mp4DataPtr_ = g_mp4DownloadBuffer;
                mp4DataSize_ = downloadedSize;
                currentSampleIdx_ = 0;
                lastFrameTime_ = std::chrono::steady_clock::now();
                isReady_.store(true);
                luna::util::CrashLogger::log("[Mp4PreviewPlayer] Ready to play %zu samples", samples_.size());
            } else {
                luna::util::CrashLogger::log("[Mp4PreviewPlayer] Failed to parse MP4 atoms");
            }
        } else {
            luna::util::CrashLogger::log("[Mp4PreviewPlayer] Failed to stream video (%d: %s)",
                                         resp.statusCode, resp.error.c_str());
        }
    });
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

    // Find video trak inside moov
    size_t moovEnd = moovStart + moovSize;
    size_t curTrak = moovStart + moovHdr;
    size_t videoTrakStart = 0, videoTrakSize = 0, videoTrakHdr = 0;
    bool foundVideoTrak = false;

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
                    if (std::memcmp(data + hdlrStart + hdlrHdr + 8, "vide", 4) == 0) {
                        videoTrakStart = tkStart;
                        videoTrakSize = tkSize;
                        videoTrakHdr = tkHdr;
                        foundVideoTrak = true;
                        break;
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

    return !samples_.empty();
}

void Mp4PreviewPlayer::update(VideoDecoder* decoder) {
    if (!isReady_.load() || !decoder || samples_.empty() || !mp4DataPtr_ || mp4DataSize_ == 0) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime_).count();
    if (elapsedMs < frameIntervalMs_) {
        return;
    }
    lastFrameTime_ = now;

    if (currentSampleIdx_ >= samples_.size()) {
        currentSampleIdx_ = 0;
    }

    const auto& sample = samples_[currentSampleIdx_];
    if (sample.offset + sample.size > mp4DataSize_) {
        currentSampleIdx_ = (currentSampleIdx_ + 1) % samples_.size();
        return;
    }

    // Build Annex-B packet
    annexBBuffer_.clear();
    const bool prependHeader = (currentSampleIdx_ == 0 || sample.isKeyframe);
    if (prependHeader && !spsPpsAnnexB_.empty()) {
        annexBBuffer_.insert(annexBBuffer_.end(), spsPpsAnnexB_.begin(), spsPpsAnnexB_.end());
    }

    const uint8_t* sampleBytes = mp4DataPtr_ + sample.offset;
    size_t cur = 0;
    while (cur + 4 < sample.size) {
        uint32_t nalSize = readBigU32(sampleBytes + cur);
        cur += 4;
        if (cur + nalSize > sample.size) break;

        annexBBuffer_.push_back(0);
        annexBBuffer_.push_back(0);
        annexBBuffer_.push_back(0);
        annexBBuffer_.push_back(1);
        annexBBuffer_.insert(annexBBuffer_.end(), sampleBytes + cur, sampleBytes + cur + nalSize);

        cur += nalSize;
    }

    if (!annexBBuffer_.empty()) {
        DecodedVideoFrame outFrame{};
        uint64_t pts = currentSampleIdx_ * 33333; // 30 FPS PTS
        if (decoder->decode(annexBBuffer_.data(), annexBBuffer_.size(), pts, outFrame)) {
            if (outFrame.width > 0 && outFrame.height > 0 && outFrame.y && outFrame.u && outFrame.v) {
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
            }
        }
    }

    currentSampleIdx_ = (currentSampleIdx_ + 1) % samples_.size();
}

} // namespace luna::media
