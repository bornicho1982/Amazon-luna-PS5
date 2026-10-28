#pragma once

#include <string>
#include <vector>
#include <utility>
#include <chrono>
#include "display/display_manager.h"

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

    std::vector<luna::display::GameTile> fetchCatalog(const std::string& accessToken,
                                                      const std::string& customerId = "",
                                                      const std::string& deviceSerial = "");

    const std::vector<luna::display::LunaCollection>& getHomeCollections() const { return m_homeCollections; }
    const std::vector<luna::display::LunaCollection>& getGameNightCollections() const { return m_gameNightCollections; }
    const std::vector<luna::display::LunaCollection>& getLibraryCollections() const { return m_libraryCollections; }
    const std::vector<luna::display::LunaCollection>& getRemotePlayCollections() const { return m_remotePlayCollections; }
    const luna::display::PlayerProfileInfo& getPlayerProfile() const { return m_playerProfile; }

    bool preloadGameDetails(luna::display::GameTile& tile,
                            const std::string& accessToken,
                            const std::string& deviceSerial = "");

    std::string fetchRawPage(const std::string& pageType,
                             const std::string& pageId,
                             const std::string& accessToken,
                             const std::string& deviceSerial = "");

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
    std::vector<luna::display::LunaCollection> fetchRemotePlayCollections(const std::string& accessToken, const std::string& deviceSerial = "");
    bool createParty(std::string& outPin, std::string& outPartyId, const std::string& accessToken, const std::string& deviceSerial = "");
    bool joinParty(const std::string& pin, const std::string& accessToken, const std::string& deviceSerial = "");

private:
    bool pollJob();
    bool fetchAppConfig(const std::string& accessToken);
    void fetchNavigationAndProfile(const std::string& accessToken, const std::string& deviceSerial);
    std::vector<luna::display::LunaCollection> fetchPageCollections(const std::string& pageType,
                                                                    const std::string& pageId,
                                                                    const std::string& accessToken,
                                                                    const std::string& deviceSerial,
                                                                    std::vector<luna::display::GameTile>& masterCatalog);
    bool resolveGameDetails(const std::string& gameId,
                            const std::string& accessToken,
                            const std::string& deviceSerial,
                            std::string& outPlayToken,
                            std::string& outBuildId,
                            std::string& outProfileId,
                            luna::display::GameTile* inoutTile = nullptr);
    bool loginPlayer(const std::string& profileId,
                     const std::string& accessToken,
                     const std::string& deviceSerial);

    SessionState m_state = SessionState::Idle;
    GameSessionInfo m_info;
    std::string m_accessToken;
    std::string m_deviceSerial;
    std::string m_profileToken;
    std::string m_tdi;
    std::string m_proxyHost = "https://proxy-prod.eu-west-1.tempo.digital.a2z.com";
    std::string m_statusMessage;

    std::vector<luna::display::LunaCollection> m_homeCollections;
    std::vector<luna::display::LunaCollection> m_gameNightCollections;
    std::vector<luna::display::LunaCollection> m_libraryCollections;
    std::vector<luna::display::LunaCollection> m_remotePlayCollections;
    luna::display::PlayerProfileInfo m_playerProfile;

    int m_pollAttempts = 0;
    static constexpr int kMaxPollAttempts = 40;
    std::chrono::steady_clock::time_point m_lastPollTime;
    int m_pollIntervalMs = 1500;
};

} // namespace luna::session
