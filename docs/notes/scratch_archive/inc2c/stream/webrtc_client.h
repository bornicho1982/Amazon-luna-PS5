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
    const StreamStats& getStats() const { return stats_; }

    void processVideoRtp(const uint8_t* pkt, size_t len);
    void processAudioRtp(const uint8_t* pkt, size_t len);

private:
    void setupPeerConnection();

    StreamConfig config_;
    StreamStats stats_{};

    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::Track> videoTrack_;
    std::shared_ptr<rtc::Track> audioTrack_;
    std::shared_ptr<rtc::DataChannel> inputChannel_;
    std::shared_ptr<rtc::DataChannel> messageChannel_;

    std::atomic<bool> isConnected_{false};
    std::atomic<bool> isGatheringComplete_{false};
    std::mutex gatheringMutex_;
    std::condition_variable gatheringCv_;

    std::mutex txMutex_;
    std::vector<uint8_t> currentAccessUnit_;
    uint32_t currentVideoTs_ = 0;

    VideoFrameCallback videoCb_;
    AudioFrameCallback audioCb_;
};

} // namespace luna::stream
