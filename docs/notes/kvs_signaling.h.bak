#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace luna::signaling {

struct IceCandidate {
    std::string candidate;
    std::string sdpMid;
    int sdpMLineIndex = 0;
};

using SdpAnswerCallback = std::function<void(const std::string& sdpAnswer)>;
using IceCandidateCallback = std::function<void(const IceCandidate& candidate)>;
using StateChangeCallback = std::function<void(bool connected)>;

class KvsSignalingClient {
public:
    KvsSignalingClient();
    ~KvsSignalingClient();

    bool connect(const std::string& wssEndpoint, const std::string& channelArn, const std::string& clientId);
    void disconnect();

    bool sendConnectAsViewer();
    bool sendSdpOffer(const std::string& sdpOffer);
    bool sendIceCandidate(const IceCandidate& candidate);

    void onAnswer(SdpAnswerCallback cb) { answerCb_ = std::move(cb); }
    void onCandidate(IceCandidateCallback cb) { candidateCb_ = std::move(cb); }
    void onStateChange(StateChangeCallback cb) { stateCb_ = std::move(cb); }

    void processIncomingMessage(const std::string& rawJson);

    bool isConnected() const { return isConnected_; }

    static std::string base64Encode(const std::string& in);
    static std::string base64Decode(const std::string& in);

private:
    bool openTlsWebSocket(const std::string& host, int port, const std::string& path);
    bool sendWsFrame(const std::string& textPayload);
    void receiveLoop();

    std::string endpoint_;
    std::string channelArn_;
    std::string clientId_ = "LunaViewerPS5";

    int tcpSock_ = -1;
    int sslCtxId_ = -1;
    int sslConnId_ = -1;
    std::atomic<bool> isConnected_{false};
    std::thread recvThread_;
    std::mutex sendMutex_;

    SdpAnswerCallback answerCb_;
    IceCandidateCallback candidateCb_;
    StateChangeCallback stateCb_;
};

} // namespace luna::signaling
