#include "signaling/kvs_signaling.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSE_SOCK(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#define CLOSE_SOCK(s) ::close(s)
#endif

#if defined(LUNA_TARGET_PS5)
extern "C" {
int sceSslInit(size_t poolSize);
int sceSslTerm(int sslCtxId);
int sceSslCreateSslConnection(int sslCtxId);
int sceSslDeleteSslConnection(int sslConnId);
int sceSslConnect(int sslConnId, int sock, const char* serverName, uint32_t flags, const void* reserved);
int sceSslSend(int sslConnId, const void* buf, size_t len);
int sceSslRecv(int sslConnId, void* buf, size_t len);
int sceSslClose(int sslConnId);
}
#endif

namespace luna::signaling {

namespace {

const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool parseWssUrl(const std::string& url, std::string& host, int& port, std::string& path) {
    size_t p = url.find("://");
    size_t hostStart = (p == std::string::npos) ? 0 : p + 3;
    size_t pathStart = url.find('/', hostStart);
    std::string hostPort = (pathStart == std::string::npos)
                               ? url.substr(hostStart)
                               : url.substr(hostStart, pathStart - hostStart);
    path = (pathStart == std::string::npos) ? "/" : url.substr(pathStart);

    size_t colon = hostPort.find(':');
    if (colon != std::string::npos) {
        host = hostPort.substr(0, colon);
        port = std::atoi(hostPort.c_str() + colon + 1);
    } else {
        host = hostPort;
        port = (url.rfind("ws://", 0) == 0) ? 80 : 443;
    }
    return !host.empty();
}

} // namespace

std::string KvsSignalingClient::base64Encode(const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (uint8_t c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kBase64Alphabet[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(kBase64Alphabet[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string KvsSignalingClient::base64Decode(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[static_cast<uint8_t>(kBase64Alphabet[i])] = i;

    int val = 0, valb = -8;
    for (uint8_t c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

KvsSignalingClient::KvsSignalingClient() = default;

KvsSignalingClient::~KvsSignalingClient() {
    disconnect();
}

bool KvsSignalingClient::openTlsWebSocket(const std::string& host, int port, const std::string& path) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &sa.sin_addr) != 1) {
        std::printf("[KVS Signaling] Conectando a host %s:%d...\n", host.c_str(), port);
        return false;
    }

    tcpSock_ = static_cast<int>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (tcpSock_ < 0) return false;

    if (::connect(tcpSock_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        CLOSE_SOCK(tcpSock_);
        tcpSock_ = -1;
        return false;
    }

#if defined(LUNA_TARGET_PS5)
    if (port == 443) {
        sslCtxId_ = sceSslInit(256 * 1024);
        if (sslCtxId_ >= 0) {
            sslConnId_ = sceSslCreateSslConnection(sslCtxId_);
            if (sslConnId_ >= 0) {
                if (sceSslConnect(sslConnId_, tcpSock_, host.c_str(), 0, nullptr) < 0) {
                    std::printf("[KVS Signaling] Error en handshake TLS con %s\n", host.c_str());
                    sceSslDeleteSslConnection(sslConnId_);
                    sslConnId_ = -1;
                }
            }
        }
    }
#endif

    // Handshake HTTP/1.1 Upgrade a WebSocket (RFC 6455)
    std::string req = "GET " + path + " HTTP/1.1\r\n"
                      "Host: " + host + ":" + std::to_string(port) + "\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                      "Sec-WebSocket-Version: 13\r\n\r\n";

#if defined(LUNA_TARGET_PS5)
    if (sslConnId_ >= 0) {
        sceSslSend(sslConnId_, req.data(), req.size());
    } else
#endif
    {
        ::send(tcpSock_, req.data(), static_cast<int>(req.size()), 0);
    }

    return true;
}

bool KvsSignalingClient::sendWsFrame(const std::string& textPayload) {
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!isConnected_) return false;

    std::vector<uint8_t> frame;
    frame.push_back(0x81); // FIN + Text frame

    size_t len = textPayload.size();
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(0x80 | len));
    } else if (len <= 65535) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    const uint8_t maskKey[4] = {0x37, 0xFA, 0x21, 0x3D};
    frame.insert(frame.end(), maskKey, maskKey + 4);

    for (size_t i = 0; i < len; ++i) {
        frame.push_back(static_cast<uint8_t>(textPayload[i]) ^ maskKey[i % 4]);
    }

    if (tcpSock_ >= 0) {
#if defined(LUNA_TARGET_PS5)
        if (sslConnId_ >= 0) {
            sceSslSend(sslConnId_, frame.data(), frame.size());
        } else
#endif
        {
            ::send(tcpSock_, reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()), 0);
        }
    }

    return true;
}

void KvsSignalingClient::receiveLoop() {
    uint8_t buf[8192];
    while (isConnected_ && tcpSock_ >= 0) {
        int r = 0;
#if defined(LUNA_TARGET_PS5)
        if (sslConnId_ >= 0) {
            r = sceSslRecv(sslConnId_, buf, sizeof(buf));
        } else
#endif
        {
            r = ::recv(tcpSock_, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        }

        if (r <= 0) break;

        if (r >= 2 && (buf[0] & 0x0F) == 0x01) {
            size_t payloadLen = buf[1] & 0x7F;
            size_t offset = 2;
            if (payloadLen == 126 && r >= 4) {
                payloadLen = (static_cast<size_t>(buf[2]) << 8) | buf[3];
                offset = 4;
            }
            if (offset + payloadLen <= static_cast<size_t>(r)) {
                std::string jsonMsg(reinterpret_cast<const char*>(buf + offset), payloadLen);
                processIncomingMessage(jsonMsg);
            }
        }
    }
}

bool KvsSignalingClient::connect(const std::string& wssEndpoint, const std::string& channelArn, const std::string& clientId) {
    disconnect();

    endpoint_ = wssEndpoint;
    channelArn_ = channelArn;
    clientId_ = clientId;
    isConnected_ = true;

    std::printf("[KVS Signaling] Conectando a AWS Kinesis WebRTC: %s\n", endpoint_.c_str());
    std::printf("[KVS Signaling] Canal ARN: %s (Client ID: %s)\n", channelArn_.c_str(), clientId_.c_str());

    std::string host, path;
    int port = 443;
    if (parseWssUrl(endpoint_, host, port, path)) {
        if (openTlsWebSocket(host, port, path)) {
            recvThread_ = std::thread(&KvsSignalingClient::receiveLoop, this);
        }
    }

    if (stateCb_) stateCb_(true);
    return sendConnectAsViewer();
}

void KvsSignalingClient::disconnect() {
    if (isConnected_.exchange(false)) {
#if defined(LUNA_TARGET_PS5)
        if (sslConnId_ >= 0) {
            sceSslClose(sslConnId_);
            sceSslDeleteSslConnection(sslConnId_);
            sslConnId_ = -1;
        }
        if (sslCtxId_ >= 0) {
            sceSslTerm(sslCtxId_);
            sslCtxId_ = -1;
        }
#endif
        if (tcpSock_ >= 0) {
            CLOSE_SOCK(tcpSock_);
            tcpSock_ = -1;
        }
        if (recvThread_.joinable()) {
            recvThread_.join();
        }
        std::printf("[KVS Signaling] Desconectado de AWS Kinesis\n");
        if (stateCb_) stateCb_(false);
    }
}

bool KvsSignalingClient::sendConnectAsViewer() {
    if (!isConnected_) return false;

    std::string msg = "{\"action\":\"CONNECT_AS_VIEWER\",\"channelArn\":\"" +
                      channelArn_ + "\",\"clientId\":\"" + clientId_ + "\"}";
    std::printf("[KVS Signaling] Enviando CONNECT_AS_VIEWER\n");
    return sendWsFrame(msg);
}

bool KvsSignalingClient::sendSdpOffer(const std::string& sdpOffer) {
    if (!isConnected_) return false;

    std::string b64Offer = base64Encode(sdpOffer);
    std::string msg = "{\"action\":\"SDP_OFFER\",\"recipientClientId\":null,\"messagePayload\":\"" +
                      b64Offer + "\"}";

    std::printf("[KVS Signaling] Enviando SDP Offer (%zu bytes orig, %zu bytes b64)\n",
                sdpOffer.size(), b64Offer.size());
    return sendWsFrame(msg);
}

bool KvsSignalingClient::sendIceCandidate(const IceCandidate& candidate) {
    if (!isConnected_) return false;

    std::string candJson = "{\"candidate\":\"" + candidate.candidate +
                           "\",\"sdpMid\":\"" + candidate.sdpMid +
                           "\",\"sdpMLineIndex\":" + std::to_string(candidate.sdpMLineIndex) + "}";

    std::string b64Candidate = base64Encode(candJson);
    std::string msg = "{\"action\":\"ICE_CANDIDATE\",\"recipientClientId\":null,\"messagePayload\":\"" +
                      b64Candidate + "\"}";

    std::printf("[KVS Signaling] ICE Candidate enviado: mid=%s index=%d\n",
                candidate.sdpMid.c_str(), candidate.sdpMLineIndex);
    return sendWsFrame(msg);
}

void KvsSignalingClient::processIncomingMessage(const std::string& rawJson) {
    if (rawJson.find("SDP_ANSWER") != std::string::npos) {
        size_t p = rawJson.find("\"messagePayload\":\"");
        if (p != std::string::npos) {
            p += 18;
            size_t end = rawJson.find("\"", p);
            if (end != std::string::npos) {
                std::string b64Answer = rawJson.substr(p, end - p);
                std::string decodedAnswer = base64Decode(b64Answer);
                std::printf("[KVS Signaling] SDP Answer decodificado (%zu bytes)\n", decodedAnswer.size());
                if (answerCb_) answerCb_(decodedAnswer);
            }
        }
    } else if (rawJson.find("ICE_CANDIDATE") != std::string::npos) {
        size_t p = rawJson.find("\"messagePayload\":\"");
        if (p != std::string::npos) {
            p += 18;
            size_t end = rawJson.find("\"", p);
            if (end != std::string::npos) {
                std::string b64Cand = rawJson.substr(p, end - p);
                std::string decodedCand = base64Decode(b64Cand);
                if (candidateCb_) {
                    IceCandidate c;
                    c.candidate = decodedCand;
                    c.sdpMid = "0";
                    c.sdpMLineIndex = 0;
                    candidateCb_(c);
                }
            }
        }
    }
}

} // namespace luna::signaling
