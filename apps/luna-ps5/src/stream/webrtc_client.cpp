#include "stream/webrtc_client.h"
#include "util/crash_logger.h"

#include <rtc/rtc.hpp>

#include <cstdio>
#include <cstring>
#include <chrono>
#include <sstream>

namespace luna::stream {

namespace {

constexpr uint8_t kNalStartCode[4] = {0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kVideoPayloadType = 108; // Amazon Luna H.264
constexpr uint8_t kAudioPayloadType = 111; // Opus 48kHz stereo

uint64_t nowSteadyMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t nowEpochMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

void appendVarint(std::vector<uint8_t>& buf, uint64_t val) {
    while (val >= 0x80) {
        buf.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
        val >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(val & 0x7F));
}

void appendTagAndBytes(std::vector<uint8_t>& out, uint32_t fieldNum, const uint8_t* data, size_t len) {
    appendVarint(out, (static_cast<uint64_t>(fieldNum) << 3) | 2u);
    appendVarint(out, len);
    out.insert(out.end(), data, data + len);
}

void appendTagAndString(std::vector<uint8_t>& out, uint32_t fieldNum, const char* str) {
    appendTagAndBytes(out, fieldNum, reinterpret_cast<const uint8_t*>(str), std::strlen(str));
}

void appendTagAndVarint(std::vector<uint8_t>& out, uint32_t fieldNum, uint64_t val) {
    appendVarint(out, (static_cast<uint64_t>(fieldNum) << 3) | 0u);
    appendVarint(out, val);
}

void writeBe32(uint8_t* dst, uint32_t val) {
    dst[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(val & 0xFF);
}

uint32_t readBe32(const uint8_t* src) {
    return (static_cast<uint32_t>(src[0]) << 24) |
           (static_cast<uint32_t>(src[1]) << 16) |
           (static_cast<uint32_t>(src[2]) << 8)  |
           static_cast<uint32_t>(src[3]);
}

class GlobalMediaRouter final : public rtc::MediaHandler {
public:
    explicit GlobalMediaRouter(WebRtcClient* client) : client_(client) {}

    void incoming(rtc::message_vector& messages, [[maybe_unused]] const rtc::message_callback& send) override {
        if (!client_) return;

        for (auto& m : messages) {
            if (!m || m->size() < 12) continue;

            if (m->type == rtc::Message::Binary) {
                const uint8_t* pkt = reinterpret_cast<const uint8_t*>(m->data());
                size_t len = m->size();
                if ((pkt[0] >> 6) != 2) continue;

                uint8_t pt = pkt[1] & 0x7F;
                uint32_t ssrc = readBe32(pkt + 8);

                if (pt == kAudioPayloadType) {
                    client_->processAudioRtp(pkt, len);
                } else if (pt >= 96 && pt <= 127) {
                    if (client_->getVideoSsrc() != ssrc && ssrc != 0) {
                        luna::util::CrashLogger::log("[WebRTC] SSRC de video detectado en flujo RTP: %u (PT=%u)", ssrc, pt);
                        client_->setVideoSsrc(ssrc);
                    }
                    client_->processVideoRtp(pkt, len);
                }
            }
        }

        // Consumir mensajes para evitar que PeerConnection::dispatchMedia descarte SSRCs cambiados
        messages.clear();

        // Enviar feedback RTCP (RR + REMB + PLI si aplica) y Ping de sincronizacion
        bool wantPli = client_->consumePliRequest();
        client_->sendRtcpFeedback(wantPli);
        client_->sendPingAndKeepalive();
    }

private:
    WebRtcClient* client_;
};

void initRtcLogging() {
    static std::atomic<bool> s_inited{false};
    if (!s_inited.exchange(true)) {
        rtc::InitLogger(rtc::LogLevel::Warning, [](rtc::LogLevel level, std::string message) {
            if (level <= rtc::LogLevel::Error) {
                luna::util::CrashLogger::log("[libdatachannel ERROR] %s", message.c_str());
            } else {
                luna::util::CrashLogger::log("[libdatachannel WARN] %s", message.c_str());
            }
        });
    }
}

} // namespace

WebRtcClient::WebRtcClient() {
    initRtcLogging();
}

WebRtcClient::~WebRtcClient() {
    stop();
}

bool WebRtcClient::init(const StreamConfig& config) {
    config_ = config;
    currentAccessUnit_.reserve(256 * 1024);
    luna::util::CrashLogger::log("[WebRTC] Inicializado (%dx%d @ %d FPS)",
                                 config_.targetWidth, config_.targetHeight, config_.targetFps);
    return true;
}

void WebRtcClient::setupPeerConnection() {
    if (pc_) return;

    luna::util::CrashLogger::logStep(2, "PeerConnection created, adding Tracks & DataChannels...");

    rtc::Configuration rtcConfig;
    rtcConfig.certificateType = rtc::CertificateType::Ecdsa;
    rtcConfig.enableIceTcp = false;
    rtcConfig.disableAutoNegotiation = true;

    pc_ = std::make_shared<rtc::PeerConnection>(rtcConfig);
    pc_->setMediaHandler(std::make_shared<GlobalMediaRouter>(this));

    pc_->onLocalCandidate([this](rtc::Candidate c) {
        std::string candStr = std::string(c);
        {
            std::lock_guard<std::mutex> lock(candidateMutex_);
            localCandidatesList_.push_back(candStr);
            localCandidateCount_ = localCandidatesList_.size();
        }
        luna::util::CrashLogger::log("[WebRTC] Candidato ICE local detectado: %s (mid=%s)",
                                     candStr.c_str(), c.mid().c_str());
    });

    pc_->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
        luna::util::CrashLogger::log("[WebRTC] Gathering state: %d", static_cast<int>(state));
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            std::lock_guard<std::mutex> lock(gatheringMutex_);
            isGatheringComplete_ = true;
            gatheringCv_.notify_all();
            luna::util::CrashLogger::log("[WebRTC] Recolección de candidatos ICE locales completada (total: %zu).",
                                         localCandidateCount_.load());
        }
    });

    pc_->onIceStateChange([this](rtc::PeerConnection::IceState state) {
        const char* stateStr = "Unknown";
        switch (state) {
            case rtc::PeerConnection::IceState::New: stateStr = "New"; break;
            case rtc::PeerConnection::IceState::Checking: stateStr = "Checking"; break;
            case rtc::PeerConnection::IceState::Connected: stateStr = "Connected"; break;
            case rtc::PeerConnection::IceState::Completed: stateStr = "Completed"; break;
            case rtc::PeerConnection::IceState::Failed: stateStr = "Failed"; break;
            case rtc::PeerConnection::IceState::Disconnected: stateStr = "Disconnected"; break;
            case rtc::PeerConnection::IceState::Closed: stateStr = "Closed"; break;
        }
        iceStateStr_ = stateStr;
        luna::util::CrashLogger::log("[WebRTC] ICE state transition: %s (%d)", stateStr, static_cast<int>(state));
    });

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        const char* stStr = "Unknown";
        switch (state) {
            case rtc::PeerConnection::State::New: stStr = "New"; break;
            case rtc::PeerConnection::State::Connecting: stStr = "Connecting"; break;
            case rtc::PeerConnection::State::Connected: stStr = "Connected"; break;
            case rtc::PeerConnection::State::Disconnected: stStr = "Disconnected"; break;
            case rtc::PeerConnection::State::Failed: stStr = "Failed"; break;
            case rtc::PeerConnection::State::Closed: stStr = "Closed"; break;
        }
        peerStateStr_ = stStr;
        luna::util::CrashLogger::log("[WebRTC] PeerConnection estado: %s (%d)", stStr, static_cast<int>(state));
        if (state == rtc::PeerConnection::State::Connected) {
            isConnected_ = true;
            luna::util::CrashLogger::log("[WebRTC] ¡Conexión WebRTC DTLS/SRTP establecida con éxito con Amazon Luna!");
        } else if (state == rtc::PeerConnection::State::Failed ||
                   state == rtc::PeerConnection::State::Disconnected ||
                   state == rtc::PeerConnection::State::Closed) {
            isConnected_ = false;
        }
    });

    // 1. Configurar Track de Audio Opus (mid: 0)
    rtc::Description::Audio a("0", rtc::Description::Direction::RecvOnly);
    a.addOpusCodec(kAudioPayloadType, "minptime=10;useinbandfec=1;stereo=1");
    audioTrack_ = pc_->addTrack(a);

    audioTrack_->onOpen([]() {
        luna::util::CrashLogger::log("[WebRTC] Track de Audio Opus abierto y listo para recibir.");
    });

    audioTrack_->onMessage([this](rtc::message_variant msg) {
        if (auto b = std::get_if<rtc::binary>(&msg)) {
            if (!b->empty()) {
                processAudioRtp(reinterpret_cast<const uint8_t*>(b->data()), b->size());
            }
        }
    });

    // 2. Configurar Track de Vídeo H.264 (mid: 1, perfil Luna Constrained Baseline 42e01f + SDPParser.modifyVideoOffer)
    rtc::Description::Video v("1", rtc::Description::Direction::RecvOnly);
    v.setBitrate(24000); // b=AS:24000 (24 Mbps)
    v.addH264Codec(kVideoPayloadType,
                   "level-asymmetry-allowed=0;packetization-mode=1;profile-level-id=42e01f;sps-pps-idr-in-keyframe=1;x-google-start-bitrate=15000");
    v.addH264Codec(100,
                   "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=640033;sps-pps-idr-in-keyframe=1;x-google-start-bitrate=15000");
    v.addH264Codec(102,
                   "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d0033;sps-pps-idr-in-keyframe=1;x-google-start-bitrate=15000");
    videoTrack_ = pc_->addTrack(v);

    videoTrack_->onOpen([this]() {
        luna::util::CrashLogger::log("[WebRTC] Track de Vídeo H.264 abierto. Solicitando IDR Keyframe + REMB inicial...");
        requestKeyframe();
        sendRtcpFeedback(true);
    });

    videoTrack_->onMessage([this](rtc::message_variant msg) {
        if (auto b = std::get_if<rtc::binary>(&msg)) {
            if (!b->empty()) {
                processVideoRtp(reinterpret_cast<const uint8_t*>(b->data()), b->size());
            }
        }
    });

    // 3. Configurar DataChannels en el orden oficial de RTCPeerConnectionManager:
    //    Stream 0 -> inputChannel (unordered=true, maxRetransmits=0)
    //    Stream 1 -> clientMessageChannel (ordered=true)
    rtc::DataChannelInit inputInit;
    inputInit.reliability.unordered = true;
    inputInit.reliability.maxRetransmits = 0;
    inputChannel_ = pc_->createDataChannel("inputChannel", inputInit);
    inputChannel_->onOpen([this]() {
        luna::util::CrashLogger::log("[WebRTC] DataChannel inputChannel (DualSense) abierto. Enviando GamePadConnectionEvent (DS4)...");
        // proto.dcv.input.ClientMessage { field 105: GamePadConnectionEvent { devicetype = DS4(1) } }
        static constexpr uint8_t kGamepadConnectMsg[5] = {0xCA, 0x06, 0x02, 0x50, 0x01};
        std::lock_guard<std::mutex> lock(txMutex_);
        if (inputChannel_ && inputChannel_->isOpen()) {
            inputChannel_->send(reinterpret_cast<const std::byte*>(kGamepadConnectMsg), sizeof(kGamepadConnectMsg));
        }
    });
    inputChannel_->onMessage([](rtc::message_variant msg) {
        if (auto b = std::get_if<rtc::binary>(&msg)) {
            if (b->size() > 16) {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(b->data()) + 16;
                luna::util::CrashLogger::log("[WebRTC inputChannel RX] %zu bytes (proto tag=0x%02X)", b->size(), p[0]);
            }
        }
    });

    messageChannel_ = pc_->createDataChannel("clientMessageChannel");
    messageChannel_->onOpen([this]() {
        luna::util::CrashLogger::log("[WebRTC] DataChannel clientMessageChannel abierto. Enviando handshake Protobuf de Amazon Luna...");
        sendLunaChannelHandshake();
        requestKeyframe();
    });
    messageChannel_->onMessage([this](rtc::message_variant msg) {
        if (auto text = std::get_if<std::string>(&msg)) {
            luna::util::CrashLogger::log("[WebRTC clientMessageChannel RX TEXT] %s", text->c_str());
        } else if (auto b = std::get_if<rtc::binary>(&msg)) {
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(b->data());
            size_t len = b->size();
            if (len > 16) {
                uint32_t payloadLen = static_cast<uint32_t>(raw[8]) |
                                      (static_cast<uint32_t>(raw[9]) << 8) |
                                      (static_cast<uint32_t>(raw[10]) << 16) |
                                      (static_cast<uint32_t>(raw[11]) << 24);
                const uint8_t* proto = raw + 16;
                size_t protoLen = len - 16;
                luna::util::CrashLogger::log(
                    "[WebRTC clientMessageChannel RX BIN] total=%zu hdrLen=%u proto[0..1]=0x%02X 0x%02X",
                    len, payloadLen, proto[0], (protoLen > 1 ? proto[1] : 0));
            } else {
                luna::util::CrashLogger::log("[WebRTC clientMessageChannel RX BIN] %zu bytes", len);
            }
            // Solicitar keyframe tras mensajes de configuracion del servidor
            if (stats_.videoFramesAssembled == 0) {
                requestKeyframe();
            }
        }
    });
}

void WebRtcClient::sendLunaChannelHandshake() {
    std::lock_guard<std::mutex> lock(txMutex_);
    if (!messageChannel_ || !messageChannel_->isOpen()) return;

    try {
        // 1. FeatureConfigurationRequest (BrowserClientMessage field 30)
        {
            std::vector<uint8_t> featInner;
            appendTagAndString(featInner, 1, "server-reconfigure");
            appendTagAndString(featInner, 1, "client-display-config-flag");

            std::vector<uint8_t> msg1;
            appendTagAndBytes(msg1, 30, featInner.data(), featInner.size());
            messageChannel_->send(reinterpret_cast<const std::byte*>(msg1.data()), msg1.size());
            luna::util::CrashLogger::log("[WebRTC Handshake 1/4] FeatureConfigurationRequest enviado (%zu bytes).", msg1.size());
        }

        // 2. DisplaySessionConfiguration (BrowserClientMessage field 40)
        {
            std::vector<uint8_t> resMsg;
            appendTagAndVarint(resMsg, 1, static_cast<uint64_t>(config_.targetWidth));  // width = 1920
            appendTagAndVarint(resMsg, 2, static_cast<uint64_t>(config_.targetHeight)); // height = 1080

            std::vector<uint8_t> dispMsg;
            appendTagAndBytes(dispMsg, 1, resMsg.data(), resMsg.size());                // resolution
            appendTagAndVarint(dispMsg, 2, static_cast<uint64_t>(config_.targetFps));   // refreshRate = 60
            appendTagAndBytes(dispMsg, 3, nullptr, 0);                                  // safeZones = {0,0,0,0}

            // VideoCodec 1: AVC (0, default in proto3), Profile High (100), Level 4.2 (42), maxRefFrames = 4
            std::vector<uint8_t> codecHigh;
            appendTagAndVarint(codecHigh, 2, 100);
            appendTagAndVarint(codecHigh, 3, 42);
            appendTagAndVarint(codecHigh, 5, 4);

            // VideoCodec 2: AVC (0), Profile Baseline (66), Level 3.1 (31), maxRefFrames = 4
            std::vector<uint8_t> codecBase;
            appendTagAndVarint(codecBase, 2, 66);
            appendTagAndVarint(codecBase, 3, 31);
            appendTagAndVarint(codecBase, 5, 4);

            std::vector<uint8_t> dscInner;
            appendTagAndVarint(dscInner, 1, 1); // negotiationId = 1
            appendTagAndBytes(dscInner, 3, dispMsg.data(), dispMsg.size());
            appendTagAndBytes(dscInner, 4, codecHigh.data(), codecHigh.size());
            appendTagAndBytes(dscInner, 4, codecBase.data(), codecBase.size());
            appendTagAndString(dscInner, 5, "dynamic-downsampling");
            appendTagAndVarint(dscInner, 6, 6000000);  // minBitsPerSecond = 6 Mbps
            appendTagAndVarint(dscInner, 7, 24000000); // maxBitsPerSecond = 24 Mbps
            appendTagAndVarint(dscInner, 8, 15000000); // startBitsPerSecond = 15 Mbps

            std::vector<uint8_t> msg2;
            appendTagAndBytes(msg2, 40, dscInner.data(), dscInner.size());
            messageChannel_->send(reinterpret_cast<const std::byte*>(msg2.data()), msg2.size());
            luna::util::CrashLogger::log("[WebRTC Handshake 2/4] DisplaySessionConfiguration 1080p60 H.264 enviado (%zu bytes).", msg2.size());
        }

        // 3. ClientContext (BrowserClientMessage field 42 -> BrowserMetadata field 1)
        {
            std::vector<uint8_t> bmInner;
            appendTagAndString(bmInner, 1, "Chrome");
            appendTagAndString(bmInner, 2, "131.0.0.0");
            appendTagAndString(bmInner, 3, "Windows");
            appendTagAndString(bmInner, 4, "unknown");
            appendTagAndString(bmInner, 5, "Windows");
            appendTagAndString(bmInner, 6, "10");

            std::vector<uint8_t> ccInner;
            appendTagAndBytes(ccInner, 1, bmInner.data(), bmInner.size());

            std::vector<uint8_t> msg3;
            appendTagAndBytes(msg3, 42, ccInner.data(), ccInner.size());
            messageChannel_->send(reinterpret_cast<const std::byte*>(msg3.data()), msg3.size());
            luna::util::CrashLogger::log("[WebRTC Handshake 3/4] ClientContext enviado (%zu bytes).", msg3.size());
        }

        // 4. Initial Ping (BrowserClientMessage field 20)
        {
            std::vector<uint8_t> pingInner;
            appendTagAndVarint(pingInner, 1, nowEpochMs());

            std::vector<uint8_t> msg4;
            appendTagAndBytes(msg4, 20, pingInner.data(), pingInner.size());
            messageChannel_->send(reinterpret_cast<const std::byte*>(msg4.data()), msg4.size());
            luna::util::CrashLogger::log("[WebRTC Handshake 4/4] TimeSynchronizer Ping inicial enviado (%zu bytes).", msg4.size());
        }

        uint64_t now = nowSteadyMs();
        channelOpenTimeMs_.store(now);
        lastPingTimeMs_.store(now);
        lastIdleResetTimeMs_.store(now);
        pingsSentCount_.store(1);
    } catch (const std::exception& e) {
        luna::util::CrashLogger::log("[WebRTC WARN] Error en sendLunaChannelHandshake: %s", e.what());
    }
}

void WebRtcClient::sendPingAndKeepalive() {
    uint64_t openTime = channelOpenTimeMs_.load();
    if (openTime == 0) return;

    uint64_t now = nowSteadyMs();
    uint32_t count = pingsSentCount_.load();
    uint64_t pingInterval = (count < 8) ? 1000 : 15000;

    bool doPing = (now - lastPingTimeMs_.load() >= pingInterval);
    bool doIdleReset = (now - lastIdleResetTimeMs_.load() >= 30000);
    if (!doPing && !doIdleReset) return;

    std::lock_guard<std::mutex> lock(txMutex_);
    if (!messageChannel_ || !messageChannel_->isOpen()) return;

    try {
        if (doPing) {
            lastPingTimeMs_.store(now);
            pingsSentCount_.fetch_add(1);

            std::vector<uint8_t> pingInner;
            appendTagAndVarint(pingInner, 1, nowEpochMs());

            std::vector<uint8_t> msg;
            appendTagAndBytes(msg, 20, pingInner.data(), pingInner.size());
            messageChannel_->send(reinterpret_cast<const std::byte*>(msg.data()), msg.size());
        }
        if (doIdleReset) {
            lastIdleResetTimeMs_.store(now);
            // BrowserClientMessage field 12: ResetIdleTimeoutRequest (empty message -> 0x62, 0x00)
            static constexpr uint8_t kResetIdleMsg[2] = {0x62, 0x00};
            messageChannel_->send(reinterpret_cast<const std::byte*>(kResetIdleMsg), sizeof(kResetIdleMsg));
        }
    } catch (...) {
    }
}

void WebRtcClient::sendRtcpFeedback(bool includePli) {
    uint32_t vSsrc = videoSsrc_.load();
    if (vSsrc == 0) return;

    uint64_t now = nowSteadyMs();
    bool noFramesYet = (stats_.videoFramesAssembled == 0);
    uint64_t elapsed = now - lastRtcpTimeMs_.load();

    if (!includePli && elapsed < 500) {
        return;
    }
    if (noFramesYet && elapsed >= 500) {
        includePli = true;
    }
    lastRtcpTimeMs_.store(now);

    try {
        // Paquete RTCP compuesto (RFC 3550):
        // [0..7]   RTCP Receiver Report (PT=201, RC=0, len=1)
        // [8..31]  RTCP PSFB REMB (PT=206, FMT=15, len=5, 24 Mbps = 0x011EDC6C)
        // [32..43] RTCP PSFB PLI (PT=206, FMT=1, len=2) [opcional]
        // [44..63] RTCP PSFB FIR (PT=206, FMT=4, len=4) [opcional]
        uint8_t buf[64]{};
        size_t totalLen = 32;

        // 1. RTCP RR (8 bytes)
        buf[0] = 0x80; // V=2, P=0, RC=0
        buf[1] = 201;  // PT=201 (RR)
        buf[2] = 0x00;
        buf[3] = 0x01; // length = 1 word (4 bytes)
        writeBe32(buf + 4, 1); // Sender SSRC = 1

        // 2. RTCP REMB (24 bytes) -> 24,000,000 bps (exp=7, mantissa=187500=0x2DC6C, numSSRC=1 -> 0x011EDC6C)
        buf[8]  = 0x8F; // V=2, P=0, FMT=15
        buf[9]  = 206;  // PT=206 (PSFB)
        buf[10] = 0x00;
        buf[11] = 0x05; // length = 5 words (20 bytes)
        writeBe32(buf + 12, 1);          // Sender SSRC = 1
        writeBe32(buf + 16, 0);          // Media Source SSRC = 0 (spec requirement)
        buf[20] = 'R';
        buf[21] = 'E';
        buf[22] = 'M';
        buf[23] = 'B';
        writeBe32(buf + 24, 0x011EDC6Cu); // numSSRC=1, brExp=7, brMantissa=187500 (24 Mbps)
        writeBe32(buf + 28, vSsrc);       // SSRC feedback

        if (includePli) {
            // 3. RTCP PLI (12 bytes)
            buf[32] = 0x81; // V=2, P=0, FMT=1 (PLI)
            buf[33] = 206;  // PT=206 (PSFB)
            buf[34] = 0x00;
            buf[35] = 0x02; // length = 2 words (8 bytes)
            writeBe32(buf + 36, 1);     // Sender SSRC = 1
            writeBe32(buf + 40, vSsrc); // Media Source SSRC

            // 4. RTCP FIR (20 bytes)
            uint8_t seq = firSeqNo_.fetch_add(1);
            buf[44] = 0x84; // V=2, P=0, FMT=4 (FIR)
            buf[45] = 206;  // PT=206 (PSFB)
            buf[46] = 0x00;
            buf[47] = 0x04; // length = 4 words (16 bytes)
            writeBe32(buf + 48, 1);     // Sender SSRC = 1
            writeBe32(buf + 52, 0);     // Media Source SSRC = 0
            writeBe32(buf + 56, vSsrc); // SSRC
            buf[60] = seq;
            buf[61] = 0;
            buf[62] = 0;
            buf[63] = 0;
            totalLen = 64;
        }

        if (videoTrack_ && videoTrack_->isOpen()) {
            videoTrack_->send(reinterpret_cast<const std::byte*>(buf), totalLen);
            if (includePli && stats_.keyframeRequests <= 10) {
                luna::util::CrashLogger::log("[WebRTC RTCP] Enviado RR + REMB(24Mbps) + PLI/FIR para SSRC=%u", vSsrc);
            }
        }
    } catch (const std::exception& e) {
        luna::util::CrashLogger::log("[WebRTC RTCP WARN] Error enviando feedback RTCP: %s", e.what());
    } catch (...) {
    }
}

std::string WebRtcClient::generateSdpOffer() {
    luna::util::CrashLogger::logStep(1, "Starting WebRTC generateSdpOffer...");
    try {
        stop();
        setupPeerConnection();

        isGatheringComplete_ = false;
        pc_->setLocalDescription(rtc::Description::Type::Offer);

        // Esperar a que ICE gathering complete para incluir candidatos locales
        std::unique_lock<std::mutex> lock(gatheringMutex_);
        gatheringCv_.wait_for(lock, std::chrono::milliseconds(2500), [this]() {
            return isGatheringComplete_.load();
        });

        auto localDesc = pc_->localDescription();
        if (!localDesc) {
            luna::util::CrashLogger::log("[WebRTC] Error al generar descripción local SDP.");
            return "";
        }

        std::string sdp = std::string(*localDesc);
        luna::util::CrashLogger::logStep(3, "Local SDP gathered and offer created.");
        luna::util::CrashLogger::log("[WebRTC] Oferta SDP generada (%zu bytes, %zu candidatos locales):",
                                     sdp.size(), localCandidateCount_.load());
        luna::util::CrashLogger::log("%s", sdp.c_str());
        return sdp;
    } catch (const std::exception& e) {
        luna::util::CrashLogger::log("[WebRTC CRITICAL] Excepción en generateSdpOffer: %s", e.what());
        return "";
    } catch (...) {
        luna::util::CrashLogger::log("[WebRTC CRITICAL] Excepción desconocida en generateSdpOffer");
        return "";
    }
}

bool WebRtcClient::applySdpAnswer(const std::string& sdpAnswer) {
    if (!pc_ || sdpAnswer.empty()) {
        luna::util::CrashLogger::log("[WebRTC] applySdpAnswer falló: SDP vacío o PeerConnection nula.");
        return false;
    }

    try {
        luna::util::CrashLogger::logStep(6, "Applying SDP Answer & ICE candidates...");
        luna::util::CrashLogger::log("[WebRTC] SDP Answer recibido (%zu bytes):", sdpAnswer.size());
        luna::util::CrashLogger::log("%s", sdpAnswer.c_str());

        // Extraer SSRC de audio y video del SDP Answer para RTCP PLI / REMB
        {
            std::istringstream iss(sdpAnswer);
            std::string line;
            int currentMedia = 0; // 1 = audio, 2 = video
            while (std::getline(iss, line)) {
                if (line.rfind("m=audio", 0) == 0) {
                    currentMedia = 1;
                } else if (line.rfind("m=video", 0) == 0) {
                    currentMedia = 2;
                } else if (line.rfind("m=application", 0) == 0) {
                    currentMedia = 3;
                } else if (line.rfind("a=ssrc:", 0) == 0) {
                    uint32_t ssrc = static_cast<uint32_t>(std::strtoul(line.c_str() + 7, nullptr, 10));
                    if (currentMedia == 1 && audioSsrc_.load() == 0) {
                        audioSsrc_.store(ssrc);
                        luna::util::CrashLogger::log("[WebRTC] SSRC de Audio extraido del SDP Answer: %u", ssrc);
                    } else if (currentMedia == 2 && videoSsrc_.load() == 0) {
                        videoSsrc_.store(ssrc);
                        luna::util::CrashLogger::log("[WebRTC] SSRC de Video extraido del SDP Answer: %u", ssrc);
                    }
                }
            }
        }

        rtc::Description answer(sdpAnswer, rtc::Description::Type::Answer);
        pc_->setRemoteDescription(answer);
        luna::util::CrashLogger::log("[WebRTC] SDP Answer aplicado con éxito en libdatachannel.");

        bool hasCandidates = (sdpAnswer.find("a=candidate:") != std::string::npos);
        if (!hasCandidates && !config_.remoteHost.empty() && config_.remoteUdpPort > 0) {
            luna::util::CrashLogger::log("[WebRTC WARN] sdpAnswer no contiene a=candidate:, inyectando fallback...");
            std::string directCand = "candidate:1 1 UDP 2122260223 " + config_.remoteHost + " " +
                                     std::to_string(config_.remoteUdpPort) + " typ host";
            addRemoteIceCandidate(directCand, "0");
            addRemoteIceCandidate(directCand, "1");
            addRemoteIceCandidate(directCand, "2");
        } else {
            luna::util::CrashLogger::log("[WebRTC] sdpAnswer contiene candidatos remotos; procesados nativamente por libdatachannel.");
            remoteCandidateCount_ = 1;
        }

        return true;
    } catch (const std::exception& e) {
        luna::util::CrashLogger::log("[WebRTC CRITICAL] Excepción al aplicar SDP Answer: %s", e.what());
        return false;
    } catch (...) {
        luna::util::CrashLogger::log("[WebRTC CRITICAL] Excepción desconocida al aplicar SDP Answer");
        return false;
    }
}

void WebRtcClient::setRemoteEndpoint(const std::string& host, int port) {
    config_.remoteHost = host;
    config_.remoteUdpPort = port;
    luna::util::CrashLogger::log("[WebRTC] Endpoint remoto configurado: %s:%d", host.c_str(), port);
}

void WebRtcClient::setRemoteIceCredentials(const std::string& ufrag, const std::string& pwd) {
    (void)ufrag;
    (void)pwd;
}

void WebRtcClient::addRemoteIceCandidate(const std::string& candidateLine, const std::string& mid) {
    if (!pc_ || candidateLine.empty()) return;
    try {
        std::string cand = candidateLine;
        while (!cand.empty() && (cand.back() == '\r' || cand.back() == '\n' || cand.back() == ' ')) {
            cand.pop_back();
        }
        pc_->addRemoteCandidate(rtc::Candidate(cand, mid));
        remoteCandidateCount_++;
        luna::util::CrashLogger::log("[WebRTC] Candidato ICE remoto añadido: %s (mid=%s)", cand.c_str(), mid.c_str());
    } catch (const std::exception& e) {
        luna::util::CrashLogger::log("[WebRTC WARN] Error añadiendo candidato ICE remoto: %s", e.what());
    }
}

bool WebRtcClient::start() {
    isConnected_ = true;
    return true;
}

void WebRtcClient::stop() {
    isConnected_ = false;
    iceStateStr_ = "Closed";
    peerStateStr_ = "Closed";
    localCandidateCount_ = 0;
    remoteCandidateCount_ = 0;
    videoSsrc_ = 0;
    audioSsrc_ = 0;
    pliRequested_ = true;
    channelOpenTimeMs_ = 0;
    lastPingTimeMs_ = 0;
    lastIdleResetTimeMs_ = 0;
    pingsSentCount_ = 0;
    {
        std::lock_guard<std::mutex> lock(candidateMutex_);
        localCandidatesList_.clear();
    }
    if (pc_) {
        pc_->close();
        pc_.reset();
    }
    videoTrack_.reset();
    audioTrack_.reset();
    inputChannel_.reset();
    messageChannel_.reset();
    {
        std::lock_guard<std::mutex> lock(videoRtpMutex_);
        currentAccessUnit_.clear();
        cachedSps_.clear();
        cachedPps_.clear();
        currentAuHasVcl_ = false;
        currentAuIsIdr_ = false;
        currentAuCorrupted_ = false;
        inFuA_ = false;
        hasLastVideoSeq_ = false;
    }
    stats_ = StreamStats{};
    std::printf("[WebRTC] Sesión WebRTC cerrada limpiamente.\n");
}

void WebRtcClient::processAudioRtp(const uint8_t* pkt, size_t len) {
    if (len < 12 || (pkt[0] >> 6) != 2) return;

    bool extension = (pkt[0] & 0x10) != 0;
    uint8_t csrcCount = pkt[0] & 0x0F;
    uint8_t payloadType = pkt[1] & 0x7F;

    if (payloadType != kAudioPayloadType) return;

    uint32_t timestamp = (static_cast<uint32_t>(pkt[4]) << 24) |
                         (static_cast<uint32_t>(pkt[5]) << 16) |
                         (static_cast<uint32_t>(pkt[6]) << 8)  |
                         static_cast<uint32_t>(pkt[7]);

    size_t offset = 12 + (csrcCount * 4);
    if (extension) {
        if (offset + 4 > len) return;
        uint16_t extLenWords = (static_cast<uint16_t>(pkt[offset + 2]) << 8) | pkt[offset + 3];
        offset += 4 + (extLenWords * 4);
    }

    if (offset >= len) return;

    const uint8_t* payload = pkt + offset;
    size_t payloadLen = len - offset;

    stats_.audioFramesReceived++;
    if (stats_.audioFramesReceived == 1 || stats_.audioFramesReceived % 300 == 0) {
        luna::util::CrashLogger::log("[WebRTC] Paquetes de audio RTP recibidos: %llu", stats_.audioFramesReceived);
    }
    if (audioCb_) {
        audioCb_(payload, payloadLen, timestamp);
    }
}

void WebRtcClient::processVideoRtp(const uint8_t* pkt, size_t len) {
    if (len < 12 || (pkt[0] >> 6) != 2) return;

    bool padding = (pkt[0] & 0x20) != 0;
    bool extension = (pkt[0] & 0x10) != 0;
    uint8_t csrcCount = pkt[0] & 0x0F;
    bool marker = (pkt[1] & 0x80) != 0;
    uint8_t payloadType = pkt[1] & 0x7F;

    if (payloadType == kAudioPayloadType || payloadType < 96) {
        return;
    }

    uint16_t seq = (static_cast<uint16_t>(pkt[2]) << 8) | static_cast<uint16_t>(pkt[3]);

    uint32_t timestamp = (static_cast<uint32_t>(pkt[4]) << 24) |
                         (static_cast<uint32_t>(pkt[5]) << 16) |
                         (static_cast<uint32_t>(pkt[6]) << 8)  |
                         static_cast<uint32_t>(pkt[7]);

    uint32_t pktSsrc = (static_cast<uint32_t>(pkt[8]) << 24) |
                       (static_cast<uint32_t>(pkt[9]) << 16) |
                       (static_cast<uint32_t>(pkt[10]) << 8) |
                       static_cast<uint32_t>(pkt[11]);

    if (pktSsrc != 0 && videoSsrc_.load() != pktSsrc) {
        videoSsrc_.store(pktSsrc);
        pliRequested_.store(true);
        luna::util::CrashLogger::log("[WebRTC] SSRC de Video detectado en RTP: %u (0x%08X)", pktSsrc, pktSsrc);
    }

    size_t offset = 12 + (csrcCount * 4);
    if (extension) {
        if (offset + 4 > len) return;
        uint16_t extLenWords = (static_cast<uint16_t>(pkt[offset + 2]) << 8) | pkt[offset + 3];
        offset += 4 + (extLenWords * 4);
    }

    size_t end = len;
    if (padding && end > offset) {
        uint8_t padBytes = pkt[len - 1];
        if (padBytes < end - offset) {
            end -= padBytes;
        }
    }
    if (offset >= end) return;

    const uint8_t* payload = pkt + offset;
    size_t payloadLen = end - offset;

    VideoFrameCallback cbToInvoke;
    std::vector<uint8_t> completedFrame;

    {
        std::lock_guard<std::mutex> lock(videoRtpMutex_);
        stats_.rtpPacketsReceived++;

        uint8_t nalHeader = payload[0];
        uint8_t nalType = nalHeader & 0x1F;

        if (stats_.rtpPacketsReceived <= 12 || stats_.rtpPacketsReceived % 300 == 0) {
            uint8_t fuHdr = (nalType == 28 && payloadLen > 1) ? payload[1] : 0;
            luna::util::CrashLogger::log("[WebRTC] Video RTP #%llu seq=%u ts=%u M=%d PT=%u len=%zu nal=%u fu=0x%02X",
                                         stats_.rtpPacketsReceived, seq, timestamp, marker ? 1 : 0,
                                         payloadType, payloadLen, nalType, fuHdr);
        }

        // Check sequence continuity
        if (hasLastVideoSeq_) {
            uint16_t expectedSeq = static_cast<uint16_t>(lastVideoSeq_ + 1);
            if (seq != expectedSeq) {
                currentAuCorrupted_ = true;
                pliRequested_.store(true);
            }
        }
        lastVideoSeq_ = seq;
        hasLastVideoSeq_ = true;

        // Timestamp boundary handling: NEVER discard SPS/PPS/SEI/AUD when timestamp advances to the slice!
        if (currentVideoTs_ != timestamp && !currentAccessUnit_.empty()) {
            if (currentAuHasVcl_) {
                // Previous slice missed its marker packet; discard incomplete VCL and request keyframe
                stats_.droppedCorruptFrames++;
                pliRequested_.store(true);
                currentAccessUnit_.clear();
                currentAuHasVcl_ = false;
                currentAuIsIdr_ = false;
                currentAuCorrupted_ = false;
                inFuA_ = false;
            }
        }
        currentVideoTs_ = timestamp;

        auto recordNalUnit = [&](const uint8_t* unitData, size_t unitLen) {
            if (unitLen == 0) return;
            uint8_t type = unitData[0] & 0x1F;
            if (type == 7) {
                stats_.spsCount++;
                cachedSps_.assign(kNalStartCode, kNalStartCode + 4);
                cachedSps_.insert(cachedSps_.end(), unitData, unitData + unitLen);
                if (stats_.spsCount <= 5) {
                    luna::util::CrashLogger::log("[WebRTC H264] SPS #%llu cacheado (%zu bytes)", stats_.spsCount, unitLen);
                }
                return;
            } else if (type == 8) {
                stats_.ppsCount++;
                cachedPps_.assign(kNalStartCode, kNalStartCode + 4);
                cachedPps_.insert(cachedPps_.end(), unitData, unitData + unitLen);
                if (stats_.ppsCount <= 5) {
                    luna::util::CrashLogger::log("[WebRTC H264] PPS #%llu cacheado (%zu bytes)", stats_.ppsCount, unitLen);
                }
                return;
            } else if (type == 5) {
                stats_.idrCount++;
                currentAuHasVcl_ = true;
                currentAuIsIdr_ = true;
            } else if (type == 1) {
                stats_.sliceCount++;
                currentAuHasVcl_ = true;
            }
            currentAccessUnit_.insert(currentAccessUnit_.end(), kNalStartCode, kNalStartCode + 4);
            currentAccessUnit_.insert(currentAccessUnit_.end(), unitData, unitData + unitLen);
        };

        // 1. Single NAL unit (types 1..23)
        if (nalType >= 1 && nalType <= 23) {
            inFuA_ = false;
            recordNalUnit(payload, payloadLen);
        }
        // 2. STAP-A aggregation packet (type 24)
        else if (nalType == 24) {
            inFuA_ = false;
            size_t i = 1;
            while (i + 2 <= payloadLen) {
                uint16_t unitSize = (static_cast<uint16_t>(payload[i]) << 8) | payload[i + 1];
                i += 2;
                if (unitSize == 0 || i + unitSize > payloadLen) break;
                recordNalUnit(payload + i, unitSize);
                i += unitSize;
            }
        }
        // 3. FU-A fragmentation unit (type 28)
        else if (nalType == 28 && payloadLen > 2) {
            uint8_t fuHeader = payload[1];
            bool startBit = (fuHeader & 0x80) != 0;
            bool endBit = (fuHeader & 0x40) != 0;
            uint8_t origNalType = fuHeader & 0x1F;
            if (startBit) {
                inFuA_ = true;
                uint8_t reconstructedNal = (nalHeader & 0xE0) | origNalType;
                if (origNalType == 5) {
                    stats_.idrCount++;
                    currentAuHasVcl_ = true;
                    currentAuIsIdr_ = true;
                } else if (origNalType == 1) {
                    stats_.sliceCount++;
                    currentAuHasVcl_ = true;
                }
                currentAccessUnit_.insert(currentAccessUnit_.end(), kNalStartCode, kNalStartCode + 4);
                currentAccessUnit_.push_back(reconstructedNal);
                currentAccessUnit_.insert(currentAccessUnit_.end(), payload + 2, payload + payloadLen);
            } else if (inFuA_) {
                currentAccessUnit_.insert(currentAccessUnit_.end(), payload + 2, payload + payloadLen);
                if (endBit) {
                    inFuA_ = false;
                }
            } else {
                // Missing FU-A start fragment
                currentAuCorrupted_ = true;
                pliRequested_.store(true);
            }
        }

        // Only emit access unit when marker=1 AND we have an actual VCL slice (not just standalone SPS/PPS!)
        if (marker && !currentAccessUnit_.empty() && currentAuHasVcl_) {
            if (currentAuCorrupted_) {
                stats_.droppedCorruptFrames++;
                pliRequested_.store(true);
            } else {
                stats_.videoFramesAssembled++;
                if (currentAuIsIdr_ && !cachedSps_.empty() && !cachedPps_.empty()) {
                    // Always prepend cached SPS + PPS to IDR keyframes so Videodec2 never misses parameter sets
                    completedFrame.reserve(cachedSps_.size() + cachedPps_.size() + currentAccessUnit_.size());
                    completedFrame.insert(completedFrame.end(), cachedSps_.begin(), cachedSps_.end());
                    completedFrame.insert(completedFrame.end(), cachedPps_.begin(), cachedPps_.end());
                    completedFrame.insert(completedFrame.end(), currentAccessUnit_.begin(), currentAccessUnit_.end());
                } else {
                    completedFrame = std::move(currentAccessUnit_);
                }
                if (stats_.videoFramesAssembled <= 5 || stats_.videoFramesAssembled % 120 == 0) {
                    luna::util::CrashLogger::log("[WebRTC] Frame H.264 #%llu ensamblado (%zu bytes, IDR=%d, SPS=%llu, PPS=%llu)",
                                                 stats_.videoFramesAssembled, completedFrame.size(),
                                                 currentAuIsIdr_ ? 1 : 0, stats_.spsCount, stats_.ppsCount);
                }
                cbToInvoke = videoCb_;
            }
            currentAccessUnit_.clear();
            currentAuHasVcl_ = false;
            currentAuIsIdr_ = false;
            currentAuCorrupted_ = false;
            inFuA_ = false;
        }
    }

    // Invoke callback OUTSIDE videoRtpMutex_ so network lock is never held during processing
    if (cbToInvoke && !completedFrame.empty()) {
        cbToInvoke(completedFrame.data(), completedFrame.size(), timestamp);
    }
}

void WebRtcClient::sendInputPacket(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;

    {
        std::lock_guard<std::mutex> lock(txMutex_);
        if (inputChannel_ && inputChannel_->isOpen()) {
            inputChannel_->send(reinterpret_cast<const std::byte*>(data), size);
        }
    }
    sendPingAndKeepalive();
}

void WebRtcClient::sendControlMessage(const std::string& jsonMsg) {
    if (jsonMsg.empty()) return;

    std::lock_guard<std::mutex> lock(txMutex_);
    if (messageChannel_ && messageChannel_->isOpen()) {
        messageChannel_->send(jsonMsg);
    }
}

void WebRtcClient::requestKeyframe() {
    stats_.keyframeRequests++;
    pliRequested_.store(true);
}

} // namespace luna::stream
