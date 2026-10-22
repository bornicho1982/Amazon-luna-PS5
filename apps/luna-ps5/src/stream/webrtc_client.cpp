#include "stream/webrtc_client.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define CLOSE_UDP(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define CLOSE_UDP(s) ::close(s)
#endif

namespace luna::stream {

namespace {
constexpr uint8_t kNalStartCode[4] = {0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kVideoPayloadType = 108; // Amazon Luna H.264
constexpr uint8_t kAudioPayloadType = 111; // Opus 48kHz stereo
} // namespace

WebRtcClient::WebRtcClient() = default;

WebRtcClient::~WebRtcClient() {
    stop();
}

bool WebRtcClient::init(const StreamConfig& config) {
    config_ = config;
    currentAccessUnit_.reserve(256 * 1024);
    std::printf("[WebRTC Client] Inicializado (%dx%d @ %d FPS, puerto UDP local=%d)\n",
                config_.targetWidth, config_.targetHeight, config_.targetFps, config_.localUdpPort);
    return true;
}

std::string WebRtcClient::generateSdpOffer() const {
    std::string portStr = std::to_string(config_.localUdpPort);
    std::string sdp;
    sdp.reserve(1536);
    sdp += "v=0\r\n";
    sdp += "o=- 4962303333 1 IN IP4 127.0.0.1\r\n";
    sdp += "s=AmazonLunaPS5\r\n";
    sdp += "t=0 0\r\n";
    sdp += "a=group:BUNDLE 0 1 2\r\n";
    sdp += "a=msid-semantic: WMS\r\n";
    sdp += "a=ice-ufrag:luna0ps5ufrag\r\n";
    sdp += "a=ice-pwd:luna0ps5password00000000000000\r\n";
    sdp += "a=ice-options:trickle\r\n";
    sdp += "a=fingerprint:sha-256 B1:B4:6B:42:F2:F2:1C:18:31:36:5A:F3:A8:10:E2:03:62:3C:D5:41:0F:7D:61:D1:D1:17:F9:6B:19:D9:63:F6\r\n";
    sdp += "a=setup:actpass\r\n";
    // Media 0: Audio Opus
    sdp += "m=audio " + portStr + " UDP/TLS/RTP/SAVPF 111\r\n";
    sdp += "c=IN IP4 0.0.0.0\r\n";
    sdp += "a=mid:0\r\n";
    sdp += "a=sendrecv\r\n";
    sdp += "a=rtcp-mux\r\n";
    sdp += "a=rtpmap:111 opus/48000/2\r\n";
    sdp += "a=fmtp:111 minptime=10;useinbandfec=1;stereo=1\r\n";
    sdp += "a=candidate:1 1 UDP 2130706431 127.0.0.1 " + portStr + " typ host\r\n";
    // Media 1: Video H.264 (Luna Profile 42e01f / Constrained Baseline)
    sdp += "m=video " + portStr + " UDP/TLS/RTP/SAVPF 108 100 102\r\n";
    sdp += "c=IN IP4 0.0.0.0\r\n";
    sdp += "a=mid:1\r\n";
    sdp += "a=recvonly\r\n";
    sdp += "a=rtcp-mux\r\n";
    sdp += "a=rtpmap:108 H264/90000\r\n";
    sdp += "a=fmtp:108 level-asymmetry-allowed=0;packetization-mode=1;profile-level-id=42e01f\r\n";
    sdp += "a=rtcp-fb:108 nack\r\n";
    sdp += "a=rtcp-fb:108 nack pli\r\n";
    sdp += "a=rtcp-fb:108 ccm fir\r\n";
    sdp += "a=rtpmap:100 H264/90000\r\n";
    sdp += "a=fmtp:100 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=640033\r\n";
    sdp += "a=rtcp-fb:100 nack\r\n";
    sdp += "a=rtcp-fb:100 nack pli\r\n";
    sdp += "a=candidate:1 1 UDP 2130706431 127.0.0.1 " + portStr + " typ host\r\n";
    // Media 2: DataChannel (Input / Control)
    sdp += "m=application " + portStr + " UDP/DTLS/SCTP webrtc-datachannel\r\n";
    sdp += "c=IN IP4 0.0.0.0\r\n";
    sdp += "a=mid:2\r\n";
    sdp += "a=sctp-port:5000\r\n";
    sdp += "a=candidate:1 1 UDP 2130706431 127.0.0.1 " + portStr + " typ host\r\n";
    return sdp;
}

bool WebRtcClient::applySdpAnswer(const std::string& sdpAnswer) {
    if (sdpAnswer.empty()) return false;
    std::printf("[WebRTC Client] SDP Answer aplicado (%zu bytes)\n", sdpAnswer.size());

    // 1. Extraer IP remota de c=IN IP4
    size_t cPos = sdpAnswer.find("c=IN IP4 ");
    if (cPos != std::string::npos) {
        size_t end = sdpAnswer.find_first_of("\r\n", cPos + 9);
        std::string ip = sdpAnswer.substr(cPos + 9, end - (cPos + 9));
        if (ip != "0.0.0.0" && ip != "127.0.0.1") {
            config_.remoteHost = ip;
        }
    }

    // 2. Extraer puerto de m=audio
    size_t mPos = sdpAnswer.find("m=audio ");
    if (mPos != std::string::npos) {
        int p = std::atoi(sdpAnswer.c_str() + mPos + 8);
        if (p > 0 && p != 9) {
            config_.remoteUdpPort = p;
        }
    }

    // 3. Extraer de candidate host si está presente
    size_t candPos = sdpAnswer.find("typ host");
    if (candPos != std::string::npos) {
        size_t lineStart = sdpAnswer.rfind("candidate:", candPos);
        if (lineStart != std::string::npos) {
            size_t lineEnd = sdpAnswer.find_first_of("\r\n", candPos);
            std::string candLine = sdpAnswer.substr(lineStart, lineEnd - lineStart);
            std::vector<std::string> parts;
            size_t p = 0;
            while (p < candLine.size()) {
                size_t next = candLine.find(' ', p);
                if (next == std::string::npos) next = candLine.size();
                if (next > p) parts.push_back(candLine.substr(p, next - p));
                p = next + 1;
            }
            if (parts.size() >= 6) {
                int port = std::atoi(parts[5].c_str());
                if (!parts[4].empty() && port > 0) {
                    config_.remoteHost = parts[4];
                    config_.remoteUdpPort = port;
                }
            }
        }
    }

    // 4. Credenciales ICE
    size_t ufragPos = sdpAnswer.find("a=ice-ufrag:");
    if (ufragPos != std::string::npos) {
        size_t end = sdpAnswer.find_first_of("\r\n", ufragPos);
        remoteUfrag_ = sdpAnswer.substr(ufragPos + 12, end - (ufragPos + 12));
    }
    size_t pwdPos = sdpAnswer.find("a=ice-pwd:");
    if (pwdPos != std::string::npos) {
        size_t end = sdpAnswer.find_first_of("\r\n", pwdPos);
        remotePwd_ = sdpAnswer.substr(pwdPos + 10, end - (pwdPos + 10));
    }

    std::printf("[WebRTC Client] Destino configurado: %s:%d (ufrag=%s)\n",
                config_.remoteHost.c_str(), config_.remoteUdpPort, remoteUfrag_.c_str());
    return true;
}

void WebRtcClient::addRemoteIceCandidate(const std::string& candidateLine) {
    // Formato: candidate:<foundation> <component> UDP <priority> <ip> <port> typ ...
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos < candidateLine.size()) {
        size_t next = candidateLine.find(' ', pos);
        if (next == std::string::npos) next = candidateLine.size();
        if (next > pos) parts.push_back(candidateLine.substr(pos, next - pos));
        pos = next + 1;
    }
    if (parts.size() >= 6) {
        int port = std::atoi(parts[5].c_str());
        if (!parts[4].empty() && port > 0) {
            config_.remoteHost = parts[4];
            config_.remoteUdpPort = port;
            std::printf("[WebRTC Client] Endpoint ICE remoto seleccionado: %s:%d\n",
                        config_.remoteHost.c_str(), config_.remoteUdpPort);
        }
    }
}

bool WebRtcClient::start() {
    if (isConnected_) return true;

    udpSocket_ = static_cast<int>(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (udpSocket_ < 0) {
        std::printf("[WebRTC Client] Error creando socket UDP\n");
        return false;
    }

    int rcvBuf = 2 * 1024 * 1024;
#if defined(_WIN32)
    setsockopt(udpSocket_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    DWORD timeoutMs = 100;
    setsockopt(udpSocket_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
#else
    setsockopt(udpSocket_, SOL_SOCKET, SO_RCVBUF, &rcvBuf, sizeof(rcvBuf));
    struct timeval tv{0, 100000};
    setsockopt(udpSocket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in localAddr{};
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(static_cast<uint16_t>(config_.localUdpPort));

    if (::bind(udpSocket_, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) < 0) {
        std::printf("[WebRTC Client] Advertencia: Puerto UDP %d en uso, usando puerto efimero\n", config_.localUdpPort);
    }

    isConnected_ = true;
    rxThread_ = std::thread(&WebRtcClient::udpReceiveLoop, this);
    std::printf("[WebRTC Client] Transporte UDP/RTP activo y escuchando\n");
    return true;
}

void WebRtcClient::stop() {
    if (isConnected_.exchange(false)) {
        if (udpSocket_ >= 0) {
            CLOSE_UDP(udpSocket_);
            udpSocket_ = -1;
        }
        if (rxThread_.joinable()) {
            rxThread_.join();
        }
        std::printf("[WebRTC Client] Sesion WebRTC cerrada (RTP recibidos=%llu, cuadros=%llu)\n",
                    static_cast<unsigned long long>(stats_.rtpPacketsReceived),
                    static_cast<unsigned long long>(stats_.videoFramesAssembled));
    }
}

void WebRtcClient::udpReceiveLoop() {
    uint8_t buf[2048];
    while (isConnected_ && udpSocket_ >= 0) {
        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        int n = ::recvfrom(udpSocket_, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n > 12) {
            processRtpPacket(buf, static_cast<size_t>(n));
        }
    }
}

void WebRtcClient::processRtpPacket(const uint8_t* pkt, size_t len) {
    if (len < 12 || (pkt[0] >> 6) != 2) return;

    bool padding = (pkt[0] & 0x20) != 0;
    bool extension = (pkt[0] & 0x10) != 0;
    uint8_t csrcCount = pkt[0] & 0x0F;
    bool marker = (pkt[1] & 0x80) != 0;
    uint8_t payloadType = pkt[1] & 0x7F;

    uint32_t timestamp = (static_cast<uint32_t>(pkt[4]) << 24) |
                         (static_cast<uint32_t>(pkt[5]) << 16) |
                         (static_cast<uint32_t>(pkt[6]) << 8)  |
                         static_cast<uint32_t>(pkt[7]);
    uint32_t ssrc = (static_cast<uint32_t>(pkt[8]) << 24) |
                    (static_cast<uint32_t>(pkt[9]) << 16) |
                    (static_cast<uint32_t>(pkt[10]) << 8) |
                    static_cast<uint32_t>(pkt[11]);

    size_t offset = 12 + (csrcCount * 4);
    if (extension) {
        if (offset + 4 > len) return;
        uint16_t extLenWords = (static_cast<uint16_t>(pkt[offset + 2]) << 8) | pkt[offset + 3];
        offset += 4 + (extLenWords * 4);
    }

    size_t end = len;
    if (padding) {
        uint8_t padBytes = pkt[len - 1];
        if (padBytes >= len - offset) return;
        end -= padBytes;
    }
    if (offset >= end) return;

    const uint8_t* payload = pkt + offset;
    size_t payloadLen = end - offset;
    stats_.rtpPacketsReceived++;

    if (payloadType == kAudioPayloadType) {
        stats_.audioFramesReceived++;
        if (audioCb_) {
            audioCb_(payload, payloadLen, timestamp);
        }
        return;
    }

    if (payloadType == kVideoPayloadType || payloadType == 100 || payloadType == 102) {
        videoSsrc_ = ssrc;
        if (currentVideoTs_ != timestamp && !currentAccessUnit_.empty()) {
            currentAccessUnit_.clear();
        }
        currentVideoTs_ = timestamp;

        uint8_t nalHeader = payload[0];
        uint8_t nalType = nalHeader & 0x1F;

        if (nalType >= 1 && nalType <= 23) {
            currentAccessUnit_.insert(currentAccessUnit_.end(), kNalStartCode, kNalStartCode + 4);
            currentAccessUnit_.insert(currentAccessUnit_.end(), payload, payload + payloadLen);
        } else if (nalType == 24) {
            size_t i = 1;
            while (i + 2 <= payloadLen) {
                uint16_t unitSize = (static_cast<uint16_t>(payload[i]) << 8) | payload[i + 1];
                i += 2;
                if (unitSize == 0 || i + unitSize > payloadLen) break;
                currentAccessUnit_.insert(currentAccessUnit_.end(), kNalStartCode, kNalStartCode + 4);
                currentAccessUnit_.insert(currentAccessUnit_.end(), payload + i, payload + i + unitSize);
                i += unitSize;
            }
        } else if (nalType == 28 && payloadLen > 2) {
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

        if (marker && !currentAccessUnit_.empty()) {
            stats_.videoFramesAssembled++;
            if (videoCb_) {
                videoCb_(currentAccessUnit_.data(), currentAccessUnit_.size(), timestamp);
            }
            currentAccessUnit_.clear();
        }
    }
}

void WebRtcClient::sendInputPacket(const uint8_t* data, size_t size) {
    if (!isConnected_ || udpSocket_ < 0 || !data || size == 0) return;

    std::lock_guard<std::mutex> lock(txMutex_);
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(static_cast<uint16_t>(config_.remoteUdpPort));
    ::inet_pton(AF_INET, config_.remoteHost.c_str(), &dest.sin_addr);

    ::sendto(udpSocket_, reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
             reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
}

void WebRtcClient::sendControlMessage(const std::string& jsonMsg) {
    if (!isConnected_ || jsonMsg.empty()) return;
    sendInputPacket(reinterpret_cast<const uint8_t*>(jsonMsg.data()), jsonMsg.size());
}

void WebRtcClient::sendRtcpPli() {
    if (!isConnected_ || udpSocket_ < 0 || videoSsrc_ == 0) return;

    uint8_t pli[12];
    pli[0] = 0x81;
    pli[1] = 206;
    pli[2] = 0x00;
    pli[3] = 0x02;
    pli[4] = 0; pli[5] = 0; pli[6] = 0; pli[7] = 1;
    pli[8]  = static_cast<uint8_t>((videoSsrc_ >> 24) & 0xFF);
    pli[9]  = static_cast<uint8_t>((videoSsrc_ >> 16) & 0xFF);
    pli[10] = static_cast<uint8_t>((videoSsrc_ >> 8) & 0xFF);
    pli[11] = static_cast<uint8_t>(videoSsrc_ & 0xFF);

    stats_.keyframeRequests++;
    sendInputPacket(pli, sizeof(pli));
}

void WebRtcClient::requestKeyframe() {
    sendRtcpPli();
}

} // namespace luna::stream
