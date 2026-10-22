#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>

namespace luna::stream {

struct StreamConfig {
    std::string remoteHost = "127.0.0.1";
    int localUdpPort = 50004;
    int remoteUdpPort = 50006;
    int targetWidth = 1920;
    int targetHeight = 1080;
    int targetFps = 60;
};

struct StreamStats {
    uint64_t rtpPacketsReceived = 0;
    uint64_t videoFramesAssembled = 0;
    uint64_t audioFramesReceived = 0;
    uint64_t nacksSent = 0;
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

    std::string generateSdpOffer() const;
    bool applySdpAnswer(const std::string& sdpAnswer);
    void setRemoteEndpoint(const std::string& host, int port) {
        config_.remoteHost = host;
        config_.remoteUdpPort = port;
    }
    void setRemoteIceCredentials(const std::string& ufrag, const std::string& pwd) {
        remoteUfrag_ = ufrag;
        remotePwd_ = pwd;
    }
    void addRemoteIceCandidate(const std::string& candidateLine);

    void sendInputPacket(const uint8_t* data, size_t size);
    void sendControlMessage(const std::string& jsonMsg);
    void requestKeyframe();

    void onVideo(VideoFrameCallback cb) { videoCb_ = std::move(cb); }
    void onAudio(AudioFrameCallback cb) { audioCb_ = std::move(cb); }

    bool isConnected() const { return isConnected_; }
    const StreamStats& getStats() const { return stats_; }

    // Procesar paquete RTP crudo (usado por el socket UDP o por pruebas unitarias)
    void processRtpPacket(const uint8_t* pkt, size_t len);

private:
    void udpReceiveLoop();
    void sendRtcpPli();

    StreamConfig config_;
    StreamStats stats_{};

    int udpSocket_ = -1;
    std::atomic<bool> isConnected_{false};
    std::thread rxThread_;
    std::mutex txMutex_;

    std::string remoteUfrag_;
    std::string remotePwd_;
    uint32_t videoSsrc_ = 0;
    uint32_t currentVideoTs_ = 0;
    std::vector<uint8_t> currentAccessUnit_;

    VideoFrameCallback videoCb_;
    AudioFrameCallback audioCb_;
};

} // namespace luna::stream
