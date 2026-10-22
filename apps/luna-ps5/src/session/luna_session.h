#pragma once

#include <string>
#include <vector>
#include <utility>
#include <chrono>

namespace luna::session {

enum class SessionState {
    Idle,
    CreatingJob,
    PollingJob,
    Ready,
    Streaming,
    Failed,
    Terminated
};

struct GameSessionInfo {
    std::string gameId;
    std::string jobId;
    std::string sessionId;
    std::string publicIp;
    int port = 0;
    std::string sdpAnswer;
    std::string iceUfrag;
    std::string icePwd;
};

class LunaSessionClient {
public:
    LunaSessionClient();
    ~LunaSessionClient();

    bool startSession(const std::string& gameId,
                      const std::string& accessToken,
                      const std::string& sdpOffer,
                      const std::string& deviceSerial = "",
                      const std::string& customerId = "");

    void tick();
    void terminateSession(const std::string& accessToken);

    SessionState getState() const { return m_state; }
    const GameSessionInfo& getSessionInfo() const { return m_info; }
    std::string getStatusMessage() const { return m_statusMessage; }

private:
    bool pollJob();
    std::string resolveProfileToken(std::vector<std::pair<std::string, std::string>>& headers,
                                    const std::string& customerId);
    std::string resolvePlayToken(const std::string& gameId,
                                 const std::vector<std::pair<std::string, std::string>>& headers);

    SessionState m_state = SessionState::Idle;
    GameSessionInfo m_info;
    std::string m_accessToken;
    std::string m_profileToken;
    std::string m_proxyHost = "https://proxy-prod.eu-west-1.tempo.digital.a2z.com";
    std::string m_statusMessage;

    int m_pollAttempts = 0;
    static constexpr int kMaxPollAttempts = 40;
    std::chrono::steady_clock::time_point m_lastPollTime;
    int m_pollIntervalMs = 1200;
};

} // namespace luna::session
