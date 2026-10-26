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

void initRtcLogging() {
    static std::once_flag s_once;
    std::call_once(s_once, [] {
        rtc::InitLogger(rtc::LogLevel::Warning, [](rtc::LogLevel level, std::string message) {
            if (level <= rtc::LogLevel::Error) {
                luna::util::CrashLogger::log("[libdatachannel ERROR] %s", message.c_str());
            } else {
                luna::util::CrashLogger::log("[libdatachannel WARN] %s", message.c_str());
            }
        });
    });
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

    pc_->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
        luna::util::CrashLogger::log("[WebRTC] Gathering state: %d", static_cast<int>(state));
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            std::lock_guard<std::mutex> lock(gatheringMutex_);
            isGatheringComplete_ = true;
            gatheringCv_.notify_all();
            luna::util::CrashLogger::log("[WebRTC] Recolección de candidatos ICE locales completada.");
        }
    });

    pc_->onIceStateChange([](rtc::PeerConnection::IceState state) {
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
        luna::util::CrashLogger::log("[WebRTC] ICE state transition: %s (%d)", stateStr, static_cast<int>(state));
    });

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        luna::util::CrashLogger::log("[WebRTC] PeerConnection estado: %d", static_cast<int>(state));
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

    // 2. Configurar Track de Vídeo H.264 (mid: 1, perfil Luna Constrained Baseline 42e01f)
    rtc::Description::Video v("1", rtc::Description::Direction::RecvOnly);
    v.addH264Codec(kVideoPayloadType, "level-asymmetry-allowed=0;packetization-mode=1;profile-level-id=42e01f");
    v.addH264Codec(100, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=640033");
    v.addH264Codec(102, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d0033");
    videoTrack_ = pc_->addTrack(v);

    videoTrack_->onOpen([this]() {
        luna::util::CrashLogger::log("[WebRTC] Track de Vídeo H.264 abierto. Solicitando IDR Keyframe inicial...");
        requestKeyframe();
    });

    videoTrack_->onMessage([this](rtc::message_variant msg) {
        if (auto b = std::get_if<rtc::binary>(&msg)) {
            if (!b->empty()) {
                processVideoRtp(reinterpret_cast<const uint8_t*>(b->data()), b->size());
            }
        }
    });

    // 3. Configurar DataChannels requeridos por Amazon Luna
    messageChannel_ = pc_->createDataChannel("clientMessageChannel");
    messageChannel_->onOpen([]() {
        luna::util::CrashLogger::log("[WebRTC] DataChannel clientMessageChannel abierto.");
    });
    messageChannel_->onMessage([](rtc::message_variant msg) {
        if (auto text = std::get_if<std::string>(&msg)) {
            luna::util::CrashLogger::log("[WebRTC MessageChannel] %s", text->c_str());
        }
    });

    rtc::DataChannelInit inputInit;
    inputInit.reliability.unordered = true;
    inputInit.reliability.maxRetransmits = 0;
    inputChannel_ = pc_->createDataChannel("inputChannel", inputInit);
    inputChannel_->onOpen([]() {
        luna::util::CrashLogger::log("[WebRTC] DataChannel inputChannel (DualSense) abierto y listo para streaming.");
    });
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
        gatheringCv_.wait_for(lock, std::chrono::milliseconds(1500), [this]() {
            return isGatheringComplete_.load();
        });

        auto localDesc = pc_->localDescription();
        if (!localDesc) {
            luna::util::CrashLogger::log("[WebRTC] Error al generar descripción local SDP.");
            return "";
        }

        std::string sdp = std::string(*localDesc);
        luna::util::CrashLogger::logStep(3, "Local SDP gathered and offer created.");
        luna::util::CrashLogger::log("[WebRTC] Oferta SDP generada con éxito con huella DTLS real (%zu bytes).", sdp.size());
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
        rtc::Description answer(sdpAnswer, rtc::Description::Type::Answer);
        pc_->setRemoteDescription(answer);
        luna::util::CrashLogger::log("[WebRTC] SDP Answer aplicado con éxito en libdatachannel (%zu bytes).", sdpAnswer.size());

        // Extraer e inyectar candidatos remotos de sdpAnswer si están presentes
        std::istringstream iss(sdpAnswer);
        std::string line;
        while (std::getline(iss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) {
                line.pop_back();
            }
            if (line.rfind("a=candidate:", 0) == 0) {
                std::string cand = line.substr(2); // quitar "a="
                addRemoteIceCandidate(cand, "0");
                addRemoteIceCandidate(cand, "1");
                addRemoteIceCandidate(cand, "2");
            }
        }

        // Si ya tenemos el endpoint remoto configurado, inyectar candidato directo
        if (!config_.remoteHost.empty() && config_.remoteUdpPort > 0) {
            std::string directCand = "candidate:1 1 UDP 2122260223 " + config_.remoteHost + " " +
                                     std::to_string(config_.remoteUdpPort) + " typ host";
            addRemoteIceCandidate(directCand, "0");
            addRemoteIceCandidate(directCand, "1");
            addRemoteIceCandidate(directCand, "2");
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
    std::printf("[WebRTC] Endpoint remoto configurado: %s:%d\n", host.c_str(), port);
    if (pc_ && !host.empty() && port > 0) {
        std::string cand = "candidate:1 1 UDP 2122260223 " + host + " " + std::to_string(port) + " typ host";
        addRemoteIceCandidate(cand, "0");
        addRemoteIceCandidate(cand, "1");
        addRemoteIceCandidate(cand, "2");
    }
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
        luna::util::CrashLogger::log("[WebRTC] Candidato ICE remoto añadido: %s (mid=%s)", cand.c_str(), mid.c_str());
    } catch (const std::exception& e) {
        luna::util::CrashLogger::log("[WebRTC ERROR] Error añadiendo candidato ICE remoto: %s", e.what());
    }
}

bool WebRtcClient::start() {
    isConnected_ = true;
    return true;
}

void WebRtcClient::stop() {
    isConnected_ = false;
    if (pc_) {
        pc_->close();
        pc_.reset();
    }
    videoTrack_.reset();
    audioTrack_.reset();
    inputChannel_.reset();
    messageChannel_.reset();
    currentAccessUnit_.clear();
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

    if (payloadType != kVideoPayloadType && payloadType != 100 && payloadType != 102) {
        return;
    }

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
    stats_.rtpPacketsReceived++;
    if (stats_.rtpPacketsReceived == 1 || stats_.rtpPacketsReceived % 300 == 0) {
        luna::util::CrashLogger::log("[WebRTC] Paquetes de video RTP recibidos: %llu", stats_.rtpPacketsReceived);
    }

    if (currentVideoTs_ != timestamp && !currentAccessUnit_.empty()) {
        currentAccessUnit_.clear();
    }
    currentVideoTs_ = timestamp;

    uint8_t nalHeader = payload[0];
    uint8_t nalType = nalHeader & 0x1F;

    // 1. Unidades NAL simples (tipos 1 a 23): anteponer start code Annex-B 0x00000001
    if (nalType >= 1 && nalType <= 23) {
        currentAccessUnit_.insert(currentAccessUnit_.end(), kNalStartCode, kNalStartCode + 4);
        currentAccessUnit_.insert(currentAccessUnit_.end(), payload, payload + payloadLen);
    }
    // 2. Paquetes de agregación STAP-A (tipo 24): separar cada NAL contenida con 0x00000001
    else if (nalType == 24) {
        size_t i = 1;
        while (i + 2 <= payloadLen) {
            uint16_t unitSize = (static_cast<uint16_t>(payload[i]) << 8) | payload[i + 1];
            i += 2;
            if (unitSize == 0 || i + unitSize > payloadLen) break;
            currentAccessUnit_.insert(currentAccessUnit_.end(), kNalStartCode, kNalStartCode + 4);
            currentAccessUnit_.insert(currentAccessUnit_.end(), payload + i, payload + i + unitSize);
            i += unitSize;
        }
    }
    // 3. Paquetes fragmentados FU-A (tipo 28): reensamblar fragmentos
    else if (nalType == 28 && payloadLen > 2) {
        uint8_t fuHeader = payload[1];
        bool startBit = (fuHeader & 0x80) != 0;
        uint8_t origNalType = fuHeader & 0x1F;
        if (startBit) {
            uint8_t reconstructedNal = (nalHeader & 0xE0) | origNalType;
            currentAccessUnit_.insert(currentAccessUnit_.end(), kNalStartCode, kNalStartCode + 4);
            currentAccessUnit_.push_back(reconstructedNal);
        }
        currentAccessUnit_.insert(currentAccessUnit_.end(), payload + 2, payload + payloadLen);
    }

    // Al llegar al marcador RTP de fin de frame (M=1), entregar el access unit a Videodec2
    if (marker && !currentAccessUnit_.empty()) {
        stats_.videoFramesAssembled++;
        if (stats_.videoFramesAssembled == 1 || stats_.videoFramesAssembled % 120 == 0) {
            luna::util::CrashLogger::log("[WebRTC] Frames H.264 ensamblados: %llu (tam: %zu bytes)",
                                         stats_.videoFramesAssembled, currentAccessUnit_.size());
        }
        if (videoCb_) {
            videoCb_(currentAccessUnit_.data(), currentAccessUnit_.size(), timestamp);
        }
        currentAccessUnit_.clear();
    }
}

void WebRtcClient::sendInputPacket(const uint8_t* data, size_t size) {
    if (!data || size == 0) return;

    std::lock_guard<std::mutex> lock(txMutex_);
    if (inputChannel_ && inputChannel_->isOpen()) {
        inputChannel_->send(reinterpret_cast<const std::byte*>(data), size);
    }
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
    if (videoTrack_) {
        videoTrack_->requestKeyframe();
        std::printf("[WebRTC] Solicitud RTCP PLI enviada al servidor de Amazon Luna.\n");
    }
}

} // namespace luna::stream
