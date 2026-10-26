#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace rtc {
class PeerConnection;
class DataChannel;
class Track;
} // namespace rtc

namespace luna::stream {

struct StreamConfig {
    std::string remoteHost = "";
    int localUdpPort = 50004;
    int remoteUdpPort = 33438;
    int targetWidth = 1920;
    int targetHeight = 1080;
    int targetFps = 60;
};

struct StreamStats {
    uint64_t rtpPacketsReceived = 0;
    uint64_t videoFramesAssembled = 0;
    uint64_t audioFramesReceived = 0;
    uint64_t keyframeRequests = 0;
    uint64_t spsCount = 0;
    uint64_t ppsCount = 0;
    uint64_t idrCount = 0;
    uint64_t sliceCount = 0;
    uint64_t droppedCorruptFrames = 0;
};

using VideoFrameCallback = std::function<void(const uint8_t* h264Data, size_t size, uint64_t timestamp)>;
using AudioFrameCallback = std::function<void(const uint8_t* opusData, size_t size, uint64_t timestamp)>;

class WebRtcClient {
public:
    WebRtcClient();
    ~WebRtcClient();

    bool init(const StreamConfig& config);
    bool start();
    void stop();

    // Generar oferta SDP real con certificados DTLS para Amazon Tempo
    std::string generateSdpOffer();

    // Aplicar respuesta SDP recibida de Amazon Tempo
    bool applySdpAnswer(const std::string& sdpAnswer);

    void setRemoteEndpoint(const std::string& host, int port);
    void setRemoteIceCredentials(const std::string& ufrag, const std::string& pwd);
    void addRemoteIceCandidate(const std::string& candidateLine, const std::string& mid = "0");

    void sendInputPacket(const uint8_t* data, size_t size);
    void sendControlMessage(const std::string& jsonMsg);
    void requestKeyframe();

    void onVideo(VideoFrameCallback cb) { videoCb_ = std::move(cb); }
    void onAudio(AudioFrameCallback cb) { audioCb_ = std::move(cb); }

    bool isConnected() const { return isConnected_.load(); }
    bool isStopping() const { return m_stopping_.load(); }
    const StreamStats& getStats() const { return stats_; }

    std::string getIceStateStr() const { return iceStateStr_; }
    std::string getPeerStateStr() const { return peerStateStr_; }
    size_t getLocalCandidateCount() const { return localCandidateCount_.load(); }
    size_t getRemoteCandidateCount() const { return remoteCandidateCount_.load(); }

    void processVideoRtp(const uint8_t* pkt, size_t len);
    void processAudioRtp(const uint8_t* pkt, size_t len);
    void sendRtcpFeedback(bool includePli);
    void sendPingAndKeepalive();

    uint32_t getVideoSsrc() const { return videoSsrc_.load(); }
    void setVideoSsrc(uint32_t ssrc) { videoSsrc_.store(ssrc); }
    bool consumePliRequest() { return pliRequested_.exchange(false); }

private:
    void setupPeerConnection();
    void sendLunaChannelHandshake();

    StreamConfig config_;
    StreamStats stats_{};

    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> videoTrack_;
    std::shared_ptr<rtc::Track> audioTrack_;
    std::shared_ptr<rtc::DataChannel> inputChannel_;
    std::shared_ptr<rtc::DataChannel> messageChannel_;

    std::atomic<bool> isConnected_{false};
    std::atomic<bool> m_stopping_{false};
    std::atomic<bool> isGatheringComplete_{false};
    std::mutex gatheringMutex_;
    std::condition_variable gatheringCv_;

    std::string iceStateStr_ = "New";
    std::string peerStateStr_ = "New";
    std::atomic<size_t> localCandidateCount_{0};
    std::atomic<size_t> remoteCandidateCount_{0};
    std::vector<std::string> localCandidatesList_;
    std::mutex candidateMutex_;

    std::atomic<uint32_t> videoSsrc_{0};
    std::atomic<uint32_t> audioSsrc_{0};
    std::atomic<bool> pliRequested_{true};
    std::atomic<uint8_t> firSeqNo_{1};
    std::atomic<uint64_t> lastRtcpTimeMs_{0};
    std::atomic<uint64_t> channelOpenTimeMs_{0};
    std::atomic<uint64_t> lastPingTimeMs_{0};
    std::atomic<uint64_t> lastIdleResetTimeMs_{0};
    std::atomic<uint32_t> pingsSentCount_{0};

    std::mutex txMutex_;
    std::mutex videoRtpMutex_;
    std::vector<uint8_t> currentAccessUnit_;
    std::vector<uint8_t> cachedSps_;
    std::vector<uint8_t> cachedPps_;
    uint32_t currentVideoTs_ = 0;
    bool currentAuHasVcl_ = false;
    bool currentAuIsIdr_ = false;
    bool currentAuCorrupted_ = false;
    bool inFuA_ = false;
    uint16_t lastVideoSeq_ = 0;
    bool hasLastVideoSeq_ = false;

    VideoFrameCallback videoCb_;
    AudioFrameCallback audioCb_;
};

} // namespace luna::stream
