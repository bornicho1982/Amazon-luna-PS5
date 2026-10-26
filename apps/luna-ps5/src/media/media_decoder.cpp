#include "media/media_decoder.h"
#include "util/crash_logger.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

#if defined(LUNA_TARGET_PS5)
#include <opus/opus.h>
extern "C" {
int sceSysmoduleLoadModule(uint32_t id);
size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t searchStart, int64_t searchEnd, size_t length, size_t alignment,
                                  int memoryType, int64_t* physicalAddress);
int sceKernelMapDirectMemory(void** address, size_t length, int protection, int flags, int64_t physicalAddress,
                             size_t alignment);
int sceKernelMapNamedFlexibleMemory(void** address, size_t length, int protection, int flags, const char* name);

// Estructuras nativas de libSceVideodec2 para PlayStation 5
struct Vdec2DecoderConfig {
    uint64_t size;
    uint32_t resourceType, codecType, profile, maxLevel;
    int32_t maxWidth, maxHeight, maxDpbFrames;
    uint32_t pipelineDepth;
    uint64_t computeQueue, cpuAffinity;
    int32_t cpuPriority;
    uint32_t optimizeProgressive, checkMemoryType, reserved;
};

struct Vdec2DecoderMemory {
    uint64_t size, cpuSize;
    void* cpu;
    uint64_t gpuSize;
    void* gpu;
    uint64_t cpuGpuSize;
    void* cpuGpu;
    uint64_t maxFrameSize;
    uint32_t frameAlignment, reserved;
};

struct Vdec2ComputeConfig {
    uint64_t size;
    uint16_t pipeId, queueId;
    uint8_t checkMemoryType, reserved0;
    uint16_t reserved1;
};

struct Vdec2ComputeMemory {
    uint64_t size, cpuGpuSize;
    void* cpuGpu;
};

struct Vdec2Input {
    uint64_t size;
    void* au;
    uint64_t auSize, pts, dts, attached;
};

struct Vdec2Frame {
    uint64_t size;
    void* buffer;
    uint64_t bufferSize;
    uint32_t accepted, reserved;
};

struct Vdec2Output {
    uint64_t size;
    uint8_t valid, error, pictureCount, padding;
    uint32_t codec, width, pitch, height, reserved;
    void* buffer;
    uint64_t bufferSize;
    uint32_t frameFormat, pitchBytes;
};

int sceVideodec2QueryComputeMemoryInfo(Vdec2ComputeMemory* memory);
int sceVideodec2AllocateComputeQueue(const Vdec2ComputeConfig* config, const Vdec2ComputeMemory* memory, void** queue);
int sceVideodec2ReleaseComputeQueue(void* queue);
int sceVideodec2QueryDecoderMemoryInfo(const Vdec2DecoderConfig* config, Vdec2DecoderMemory* memory);
int sceVideodec2CreateDecoder(const Vdec2DecoderConfig* config, const Vdec2DecoderMemory* memory, void** decoder);
int sceVideodec2DeleteDecoder(void* decoder);
int sceVideodec2Decode(void* decoder, Vdec2Input* input, Vdec2Frame* frame, Vdec2Output* output);
int sceVideodec2Flush(void* decoder, Vdec2Frame* frame, Vdec2Output* output);
}
#endif

namespace luna::media {

namespace {
#if defined(LUNA_TARGET_PS5)
constexpr uint32_t kVideodecModule = 207;
constexpr int kDirectType = 12;
constexpr int kCpuGpuRw = 0x33;
constexpr int kGpuRwCpuWrite = 0x32;

size_t align16k(size_t n) {
    return (n + 0x3FFF) & ~static_cast<size_t>(0x3FFF);
}

#if defined(LUNA_TARGET_PS5)
static Vdec2DecoderConfig s_vdecCfg{};
static Vdec2DecoderMemory s_vdecMem{};
#endif

void* allocDirectMemory(size_t size, int protection) {
    int64_t start = 0;
    int rc = sceKernelAllocateDirectMemory(0, static_cast<int64_t>(sceKernelGetDirectMemorySize()), size, 0x4000,
                                           kDirectType, &start);
    void* addr = nullptr;
    if (rc == 0) {
        rc = sceKernelMapDirectMemory(&addr, size, protection, 0, start, 0x4000);
    }
    if (rc != 0) {
        std::printf("[VideoDecoder] Error en memoria directa (%zu bytes): 0x%08X\n", size, static_cast<unsigned>(rc));
        return nullptr;
    }
    return addr;
}
#endif
} // namespace

// --- Decodificador de Vídeo H.264 --------------------------------------------

VideoDecoder::VideoDecoder() = default;

VideoDecoder::~VideoDecoder() {
    shutdown();
}

bool VideoDecoder::init(int maxWidth, int maxHeight) {
    resetStreamState();

    fallbackY_.resize(1920 * 1080, 0x10);
    fallbackU_.resize(960 * 540, 0x80);
    fallbackV_.resize(960 * 540, 0x80);
    planeY_.resize(1920 * 1088, 0x10);
    planeU_.resize(960 * 544, 0x80);
    planeV_.resize(960 * 544, 0x80);
    uvRowBuf_.resize(4096, 0x80);

#if defined(LUNA_TARGET_PS5)
    int rc = sceSysmoduleLoadModule(kVideodecModule);
    luna::util::CrashLogger::log("[VideoDecoder] sceSysmoduleLoadModule(%u) -> 0x%08X",
                                 kVideodecModule, static_cast<unsigned>(rc));
    if (rc == 0 || rc == static_cast<int>(0x805A1000)) {
        Vdec2ComputeMemory compute{};
        compute.size = sizeof(compute);
        int qRc = sceVideodec2QueryComputeMemoryInfo(&compute);
        luna::util::CrashLogger::log("[VideoDecoder] QueryComputeMemoryInfo -> 0x%08X (cpuGpuSize=%llu)",
                                     static_cast<unsigned>(qRc), static_cast<unsigned long long>(compute.cpuGpuSize));
        if (qRc == 0) {
            size_t computeSize = align16k(compute.cpuGpuSize);
            compute.cpuGpu = allocDirectMemory(computeSize, kCpuGpuRw);
            compute.cpuGpuSize = computeSize;

            Vdec2ComputeConfig computeCfg{};
            computeCfg.size = sizeof(computeCfg);
            int aRc = compute.cpuGpu ? sceVideodec2AllocateComputeQueue(&computeCfg, &compute, &computeQueue_) : -1;
            luna::util::CrashLogger::log("[VideoDecoder] AllocateComputeQueue -> 0x%08X", static_cast<unsigned>(aRc));
            if (aRc == 0) {
                Vdec2DecoderConfig cfg{};
                cfg.size = sizeof(cfg);
                cfg.resourceType = 1;
                cfg.codecType = 1; // H.264 AVC
                cfg.profile = 100; // High Profile
                cfg.maxLevel = 51; // Level 5.1
                cfg.maxWidth = maxWidth;
                cfg.maxHeight = maxHeight;
                cfg.maxDpbFrames = 8;
                cfg.pipelineDepth = 1;
                cfg.computeQueue = reinterpret_cast<uint64_t>(computeQueue_);
                cfg.cpuPriority = 700;
                cfg.optimizeProgressive = 1;

                Vdec2DecoderMemory mem{};
                mem.size = sizeof(mem);
                int mRc = sceVideodec2QueryDecoderMemoryInfo(&cfg, &mem);
                if (mRc != 0) {
                    cfg.maxDpbFrames = 4;
                    mem = Vdec2DecoderMemory{};
                    mem.size = sizeof(mem);
                    mRc = sceVideodec2QueryDecoderMemoryInfo(&cfg, &mem);
                }
                luna::util::CrashLogger::log("[VideoDecoder] QueryDecoderMemoryInfo(dpb=%d) -> 0x%08X (cpu=%llu gpu=%llu cpuGpu=%llu frame=%llu)",
                                             cfg.maxDpbFrames, static_cast<unsigned>(mRc),
                                             static_cast<unsigned long long>(mem.cpuSize),
                                             static_cast<unsigned long long>(mem.gpuSize),
                                             static_cast<unsigned long long>(mem.cpuGpuSize),
                                             static_cast<unsigned long long>(mem.maxFrameSize));
                if (mRc == 0) {
                    size_t cpuSize = align16k(mem.cpuSize);
                    int fRc = sceKernelMapNamedFlexibleMemory(&mem.cpu, cpuSize, 0x03, 0, "LunaVdecCpu");
                    mem.gpuSize = align16k(mem.gpuSize);
                    mem.gpu = allocDirectMemory(mem.gpuSize, kGpuRwCpuWrite);
                    if (mem.cpuGpuSize) {
                        mem.cpuGpuSize = align16k(mem.cpuGpuSize);
                        mem.cpuGpu = allocDirectMemory(mem.cpuGpuSize, kCpuGpuRw);
                    }
                    frameBufferSize_ = align16k(mem.maxFrameSize);
                    frameBuffer_ = static_cast<uint8_t*>(allocDirectMemory(frameBufferSize_, kCpuGpuRw));

                    if (fRc == 0 && mem.cpu && mem.gpu && frameBuffer_) {
                        int cRc = sceVideodec2CreateDecoder(&cfg, &mem, &decoder_);
                        luna::util::CrashLogger::log("[VideoDecoder] sceVideodec2CreateDecoder -> 0x%08X", static_cast<unsigned>(cRc));
                        if (cRc == 0 && decoder_) {
                            s_vdecCfg = cfg;
                            s_vdecMem = mem;
                            hwActive_ = true;
                            luna::util::CrashLogger::log("[VideoDecoder] Decodificador Hardware PS5 (libSceVideodec2) ACTIVO (%dx%d, dpb=%d)",
                                                         maxWidth, maxHeight, cfg.maxDpbFrames);
                        }
                    } else {
                        luna::util::CrashLogger::log("[VideoDecoder] Error reservando memoria para Videodec2 (fRc=0x%08X)",
                                                     static_cast<unsigned>(fRc));
                    }
                }
            }
        }
    }
    if (!hwActive_) {
        luna::util::CrashLogger::log("[VideoDecoder] Advertencia: libSceVideodec2 no disponible");
    }
#else
    (void)maxWidth; (void)maxHeight;
    std::printf("[VideoDecoder] Decodificador H.264 inicializado en modo Desktop\n");
#endif

    isReady_ = true;
    return true;
}

void VideoDecoder::resetStreamState() {
    std::lock_guard<std::mutex> lock(decodeMutex_);
    needsKeyframe_.store(true);
    hasReceivedKeyframe_ = false;
    lastDecodeRc_.store(0);
    lastDecodeError_.store(0);
    decodedFrames_.store(0);

#if defined(LUNA_TARGET_PS5)
    if (hwActive_ && decoder_) {
        Vdec2Frame flushFrame{};
        flushFrame.size = sizeof(flushFrame);
        Vdec2Output flushOutput{};
        flushOutput.size = sizeof(flushOutput);
        for (int i = 0; i < 16; ++i) {
            int rc = sceVideodec2Flush(decoder_, &flushFrame, &flushOutput);
            if (rc != 0 || flushOutput.valid == 0) break;
        }

        // Recrear decoder con memoria ya mapeada para limpiar DPB y estado entre juegos
        sceVideodec2DeleteDecoder(decoder_);
        decoder_ = nullptr;
        if (s_vdecMem.cpu && s_vdecMem.gpu && frameBuffer_) {
            int cRc = sceVideodec2CreateDecoder(&s_vdecCfg, &s_vdecMem, &decoder_);
            luna::util::CrashLogger::log("[VideoDecoder] sceVideodec2CreateDecoder recreado para nuevo juego -> 0x%08X", static_cast<unsigned>(cRc));
            if (cRc != 0 || !decoder_) {
                hwActive_ = false;
                luna::util::CrashLogger::log("[VideoDecoder ERROR] Error recreando Videodec2: 0x%08X", static_cast<unsigned>(cRc));
            }
        }
    }
#endif
}

void VideoDecoder::shutdown() {
    if (isReady_) {
#if defined(LUNA_TARGET_PS5)
        if (decoder_) {
            sceVideodec2DeleteDecoder(decoder_);
            decoder_ = nullptr;
        }
        if (computeQueue_) {
            sceVideodec2ReleaseComputeQueue(computeQueue_);
            computeQueue_ = nullptr;
        }
#endif
        hwActive_ = false;
        isReady_ = false;
        std::printf("[VideoDecoder] Decodificador H.264 cerrado\n");
    }
}

bool VideoDecoder::decode(const uint8_t* h264Data, size_t size, uint64_t pts, DecodedVideoFrame& outFrame) {
    if (!isReady_ || !h264Data || size < 4) return false;
    std::lock_guard<std::mutex> lock(decodeMutex_);

    bool hasSps = false;
    bool hasPps = false;
    bool hasIdr = false;
    bool hasSlice = false;

    // Inspect NAL unit types in the Annex-B Access Unit
    for (size_t i = 0; i + 4 < size; ++i) {
        if (h264Data[i] == 0 && h264Data[i + 1] == 0 && h264Data[i + 2] == 1) {
            uint8_t nalType = h264Data[i + 3] & 0x1F;
            if (nalType == 7) hasSps = true;
            else if (nalType == 8) hasPps = true;
            else if (nalType == 5) hasIdr = true;
            else if (nalType == 1) hasSlice = true;
            i += 3;
        }
    }

    if (hasIdr && hasSps && hasPps) {
        hasReceivedKeyframe_ = true;
        needsKeyframe_.store(false);
    }

    // Do not feed non-VCL packets or orphan P-frames before the first complete IDR keyframe
    if (!hasIdr && !hasSlice) {
        return false;
    }
    if (!hasReceivedKeyframe_) {
        needsKeyframe_.store(true);
        return false;
    }

#if defined(LUNA_TARGET_PS5)
    if (hwActive_ && decoder_) {
        Vdec2Input in{sizeof(Vdec2Input), const_cast<uint8_t*>(h264Data), size, pts, pts, 0};
        Vdec2Frame frame{sizeof(Vdec2Frame), frameBuffer_, frameBufferSize_, 0, 0};
        Vdec2Output info{};
        info.size = sizeof(info);

        int rc = sceVideodec2Decode(decoder_, &in, &frame, &info);
        if (rc == 0 && !info.valid) {
            info = Vdec2Output{};
            info.size = sizeof(info);
            rc = sceVideodec2Flush(decoder_, &frame, &info);
        }

        lastDecodeRc_.store(static_cast<uint32_t>(rc));
        lastDecodeError_.store(info.error);

        static int s_vdecLogged = 0;
        if (s_vdecLogged < 25 || (rc != 0 && s_vdecLogged < 50)) {
            s_vdecLogged++;
            luna::util::CrashLogger::log("[VideoDecoder] Vdec2 rc=0x%08X valid=%u err=%u pic=%u %ux%u pitch=%u pitchB=%u fmt=%u bufSz=%llu auSz=%zu idr=%d",
                                         static_cast<unsigned>(rc), info.valid, info.error, info.pictureCount,
                                         info.width, info.height, info.pitch, info.pitchBytes,
                                         info.frameFormat, static_cast<unsigned long long>(info.bufferSize),
                                         size, hasIdr ? 1 : 0);
        }

        if (rc != 0 || !info.valid || info.error != 0 || !info.buffer || info.width == 0 || info.height == 0) {
            if (rc != 0 || info.error != 0) {
                needsKeyframe_.store(true);
            }
            return false;
        }

        const auto* rawBuf = static_cast<const uint8_t*>(info.buffer);
        int w = static_cast<int>(info.width);
        int rawH = static_cast<int>(info.height);
        int h = (rawH == 1088) ? 1080 : rawH;
        int codedH = (rawH + 15) & ~15;
        int pitch = static_cast<int>(info.pitchBytes ? info.pitchBytes : info.pitch);
        if (pitch < w) pitch = w;

        if (info.bufferSize > 0 && static_cast<size_t>(pitch) * (codedH + h / 2) > info.bufferSize) {
            codedH = rawH;
        }

        if (planeY_.size() < static_cast<size_t>(w * h)) {
            planeY_.resize(static_cast<size_t>(w * h));
        }

        // Fast sequential SIMD row-by-row copy from DirectMemory to cached CPU RAM
        for (int r = 0; r < h; ++r) {
            std::memcpy(planeY_.data() + static_cast<size_t>(r) * w,
                        rawBuf + static_cast<size_t>(r) * pitch,
                        static_cast<size_t>(w));
        }

        // Verify the decoded luma plane is not an uninitialized all-zero buffer
        uint8_t maxLuma = 0;
        const size_t yTotal = static_cast<size_t>(w) * h;
        const size_t step = std::max<size_t>(1, yTotal / 32);
        for (size_t idx = 0; idx < yTotal; idx += step) {
            if (planeY_[idx] > maxLuma) {
                maxLuma = planeY_[idx];
            }
        }
        if (maxLuma == 0) {
            needsKeyframe_.store(true);
            return false;
        }

        const uint8_t* uvBase = rawBuf + (static_cast<size_t>(pitch) * codedH);
        int chromaW = w / 2;
        int chromaH = h / 2;
        if (planeU_.size() < static_cast<size_t>(chromaW * chromaH)) {
            planeU_.resize(static_cast<size_t>(chromaW * chromaH));
            planeV_.resize(static_cast<size_t>(chromaW * chromaH));
        }
        if (uvRowBuf_.size() < static_cast<size_t>(w)) {
            uvRowBuf_.resize(static_cast<size_t>(w));
        }

        for (int r = 0; r < chromaH; ++r) {
            std::memcpy(uvRowBuf_.data(), uvBase + (static_cast<size_t>(r) * pitch), static_cast<size_t>(w));
            uint8_t* dstU = planeU_.data() + (static_cast<size_t>(r) * chromaW);
            uint8_t* dstV = planeV_.data() + (static_cast<size_t>(r) * chromaW);
            for (int c = 0; c < chromaW; ++c) {
                dstU[c] = uvRowBuf_[c * 2];
                dstV[c] = uvRowBuf_[c * 2 + 1];
            }
        }

        outFrame.y = planeY_.data();
        outFrame.u = planeU_.data();
        outFrame.v = planeV_.data();
        outFrame.strideY = w;
        outFrame.strideU = chromaW;
        outFrame.strideV = chromaW;
        outFrame.width = w;
        outFrame.height = h;
        outFrame.pts = pts;
        decodedFrames_.fetch_add(1);
        return true;
    }
    return false;
#else
    outFrame.y = fallbackY_.data();
    outFrame.u = fallbackU_.data();
    outFrame.v = fallbackV_.data();
    outFrame.strideY = 1920;
    outFrame.strideU = 960;
    outFrame.strideV = 960;
    outFrame.width = 1920;
    outFrame.height = 1080;
    outFrame.pts = pts;
    return true;
#endif
}

// --- Decodificador de Audio Opus ---------------------------------------------

AudioDecoder::AudioDecoder() = default;

AudioDecoder::~AudioDecoder() {
    shutdown();
}

bool AudioDecoder::init(int sampleRate, int channels) {
    shutdown();
    sampleRate_ = sampleRate;
    channels_ = channels;
    frameCounter_ = 0;

#if defined(LUNA_TARGET_PS5)
    int err = OPUS_OK;
    OpusDecoder* dec = opus_decoder_create(sampleRate_, channels_, &err);
    if (!dec || err != OPUS_OK) {
        luna::util::CrashLogger::log("[AudioDecoder] Error en opus_decoder_create: %d", err);
        return false;
    }
    opusDec_ = dec;
    luna::util::CrashLogger::log("[AudioDecoder] libopus 1.6.1 inicializado (%d Hz, %d canales)", sampleRate_, channels_);
#endif

    isReady_ = true;
    std::printf("[AudioDecoder] Decodificador de tramas de audio inicializado (%d Hz, %d canales)\n",
                sampleRate_, channels_);
    return true;
}

void AudioDecoder::shutdown() {
    if (isReady_) {
#if defined(LUNA_TARGET_PS5)
        if (opusDec_) {
            opus_decoder_destroy(static_cast<OpusDecoder*>(opusDec_));
            opusDec_ = nullptr;
        }
#endif
        isReady_ = false;
        std::printf("[AudioDecoder] Decodificador de audio cerrado\n");
    }
}

bool AudioDecoder::decode(const uint8_t* opusData, size_t size, std::vector<int16_t>& pcmOut) {
    if (!isReady_ || !opusData || size == 0) return false;

#if defined(LUNA_TARGET_PS5)
    if (!opusDec_) return false;
    constexpr int kMaxFrameSamples = 5760; // Up to 120ms at 48kHz
    pcmOut.resize(static_cast<size_t>(kMaxFrameSamples * channels_));
    int decodedSamples = opus_decode(
        static_cast<OpusDecoder*>(opusDec_),
        opusData,
        static_cast<opus_int32>(size),
        pcmOut.data(),
        kMaxFrameSamples,
        0
    );
    if (decodedSamples <= 0) {
        pcmOut.clear();
        return false;
    }
    pcmOut.resize(static_cast<size_t>(decodedSamples * channels_));
    frameCounter_++;
    if (frameCounter_ == 1 || frameCounter_ % 500 == 0) {
        luna::util::CrashLogger::log("[AudioDecoder] Tramas Opus decodificadas: %u (ultima: %zu B -> %d muestras/canal)",
                                     frameCounter_, size, decodedSamples);
    }
    return true;
#else
    constexpr size_t kSamplesPerChannel = 960;
    pcmOut.assign(kSamplesPerChannel * channels_, 0);
    frameCounter_++;
    return true;
#endif
}

} // namespace luna::media
