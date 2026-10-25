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
    needsKeyframe_ = true;

    fallbackY_.resize(1920 * 1080, 0x10);
    fallbackU_.resize(960 * 540, 0x80);
    fallbackV_.resize(960 * 540, 0x80);
    planeU_.resize(960 * 544, 0x80);
    planeV_.resize(960 * 544, 0x80);

#if defined(LUNA_TARGET_PS5)
    int rc = sceSysmoduleLoadModule(kVideodecModule);
    if (rc == 0) {
        Vdec2ComputeMemory compute{};
        compute.size = sizeof(compute);
        if (sceVideodec2QueryComputeMemoryInfo(&compute) == 0) {
            size_t computeSize = align16k(compute.cpuGpuSize);
            compute.cpuGpu = allocDirectMemory(computeSize, kCpuGpuRw);
            compute.cpuGpuSize = computeSize;

            Vdec2ComputeConfig computeCfg{};
            computeCfg.size = sizeof(computeCfg);
            if (compute.cpuGpu && sceVideodec2AllocateComputeQueue(&computeCfg, &compute, &computeQueue_) == 0) {
                Vdec2DecoderConfig cfg{};
                cfg.size = sizeof(cfg);
                cfg.resourceType = 1;
                cfg.codecType = 1; // H.264 AVC
                cfg.profile = 100; // High Profile
                cfg.maxLevel = 51; // Level 5.1
                cfg.maxWidth = maxWidth;
                cfg.maxHeight = maxHeight;
                cfg.maxDpbFrames = 4;
                cfg.pipelineDepth = 1;
                cfg.computeQueue = reinterpret_cast<uint64_t>(computeQueue_);
                cfg.cpuPriority = 700;
                cfg.optimizeProgressive = 1;

                Vdec2DecoderMemory mem{};
                mem.size = sizeof(mem);
                if (sceVideodec2QueryDecoderMemoryInfo(&cfg, &mem) == 0) {
                    size_t cpuSize = align16k(mem.cpuSize);
                    sceKernelMapNamedFlexibleMemory(&mem.cpu, cpuSize, 0x03, 0, "LunaVdecCpu");
                    mem.gpuSize = align16k(mem.gpuSize);
                    mem.gpu = allocDirectMemory(mem.gpuSize, kGpuRwCpuWrite);
                    if (mem.cpuGpuSize) {
                        mem.cpuGpuSize = align16k(mem.cpuGpuSize);
                        mem.cpuGpu = allocDirectMemory(mem.cpuGpuSize, kCpuGpuRw);
                    }
                    frameBufferSize_ = align16k(mem.maxFrameSize);
                    frameBuffer_ = static_cast<uint8_t*>(allocDirectMemory(frameBufferSize_, kCpuGpuRw));

                    if (mem.cpu && mem.gpu && frameBuffer_ &&
                        sceVideodec2CreateDecoder(&cfg, &mem, &decoder_) == 0) {
                        hwActive_ = true;
                        std::printf("[VideoDecoder] Decodificador Hardware PS5 (libSceVideodec2) ACTIVO (%dx%d)\n",
                                    maxWidth, maxHeight);
                    }
                }
            }
        }
    }
    if (!hwActive_) {
        std::printf("[VideoDecoder] Advertencia: libSceVideodec2 no disponible, usando ruta de respaldo\n");
    }
#else
    (void)maxWidth; (void)maxHeight;
    std::printf("[VideoDecoder] Decodificador H.264 inicializado en modo Desktop\n");
#endif

    isReady_ = true;
    return true;
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

    // Detectar keyframes en el bitstream NAL (SPS=7, IDR=5)
    for (size_t i = 0; i + 4 < size; ++i) {
        if (h264Data[i] == 0 && h264Data[i + 1] == 0 && h264Data[i + 2] == 1) {
            uint8_t nalType = h264Data[i + 3] & 0x1F;
            if (nalType == 5 || nalType == 7) {
                needsKeyframe_ = false;
            }
        }
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

        static int s_vdecLogged = 0;
        if (s_vdecLogged < 15 && (rc != 0 || info.valid)) {
            s_vdecLogged++;
            luna::util::CrashLogger::log("[VideoDecoder] sceVideodec2 rc=0x%08X valid=%u err=%u pic=%u %ux%u pitch=%u fmt=%u auSize=%zu",
                                         static_cast<unsigned>(rc), info.valid, info.error, info.pictureCount,
                                         info.width, info.height, info.pitchBytes ? info.pitchBytes : info.pitch,
                                         info.frameFormat, size);
        }

        if (rc == 0 && info.valid && info.buffer) {
            const auto* rawBuf = static_cast<const uint8_t*>(info.buffer);
            int w = static_cast<int>(info.width);
            int h = static_cast<int>(info.height);
            int pitch = static_cast<int>(info.pitchBytes ? info.pitchBytes : info.pitch);

            // Desentrelazar plano UV (NV12) a planos U y V separados (YUV420P)
            const uint8_t* uvBase = rawBuf + (static_cast<size_t>(pitch) * h);
            int chromaW = w / 2;
            int chromaH = h / 2;
            if (planeU_.size() < static_cast<size_t>(chromaW * chromaH)) {
                planeU_.resize(chromaW * chromaH);
                planeV_.resize(chromaW * chromaH);
            }

            for (int r = 0; r < chromaH; ++r) {
                const uint8_t* srcRow = uvBase + (r * pitch);
                uint8_t* dstU = planeU_.data() + (r * chromaW);
                uint8_t* dstV = planeV_.data() + (r * chromaW);
                for (int c = 0; c < chromaW; ++c) {
                    dstU[c] = srcRow[c * 2];
                    dstV[c] = srcRow[c * 2 + 1];
                }
            }

            outFrame.y = rawBuf;
            outFrame.u = planeU_.data();
            outFrame.v = planeV_.data();
            outFrame.strideY = pitch;
            outFrame.strideU = chromaW;
            outFrame.strideV = chromaW;
            outFrame.width = w;
            outFrame.height = h;
            outFrame.pts = pts;
            return true;
        }
        return false;
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
