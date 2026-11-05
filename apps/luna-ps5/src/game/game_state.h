#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include "display/display_manager.h"

namespace luna::game {

/**
 * @brief Centralizado GameState singleton con thread safety
 * 
 * Reemplaza variables globales dispersas proporcionando punto único de acceso al estado
 */
class GameState {
public:
    // Singleton pattern con thread safety
    static GameState& instance();
    ~GameState();
    
    // Prevent copying
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;

    // ============================================================================
    // Session State Management
    // ============================================================================
    
    enum class SessionState {
        Idle,
        CreatingJob,
        PollingJob,
        Ready,
        Streaming,
        Failed,
        Terminated
    };
    
    enum class ViewState {
        LoginCBL,
        CBLLoginSuccess,
        Catalog,
        GameDetail,
        MediaViewer,
        Settings,
        Profile,
        StreamingDashboard,
        Error
    };

    // Session state accessors (thread-safe)
    SessionState sessionState() const { return m_sessionState; }
    void setSessionState(SessionState state);
    
    ViewState currentView() const { return m_currentView; }
    void setCurrentView(ViewState view);

    // Session info accessors
    const GameSessionInfo& getSessionInfo() const { return m_sessionInfo; }
    void setSessionInfo(const GameSessionInfo& info);
    
    bool hasSessionInfo() const { return !m_sessionInfo.gameId.empty(); }

    // ============================================================================
    // Streaming State
    // ============================================================================
    
    bool isStreaming() const { return m_isStreaming.load(); }
    void setIsStreaming(bool streaming) { m_isStreaming.store(streaming); }
    
    uint64_t getLatency() const { return m_latency.load(); }
    void updateLatency(uint64_t latency) { m_latency.store(latency); }
    
    uint64_t getBitrate() const { return m_bitrate.load(); }
    void updateBitrate(uint64_t bitrate) { m_bitrate.store(bitrate); }
    
    void updateStreamingMetrics(uint64_t latency, uint64_t bitrate);

    // ============================================================================
    // UI State
    // ============================================================================
    
    int getMediaViewerIndex() const { return m_mediaViewerIdx; }
    void setMediaViewerIndex(int index) { m_mediaViewerIdx = index; }
    
    std::vector<std::string> getFavoriteIds() const { return m_favoriteIds; }
    void addFavoriteId(const std::string& id);
    void removeFavoriteId(const std::string& id);

    // ============================================================================
    // Frame Buffer Management (Zero-Copy Access)
    // ============================================================================
    
    // Get latest frame buffers for rendering (zero-copy access)
    const std::vector<uint8_t>& getLatestFrameY() const { return m_latestFrameY; }
    const std::vector<uint8_t>& getLatestFrameU() const { return m_latestFrameU; }
    const std::vector<uint8_t>& getLatestFrameV() const { return m_latestFrameV; }
    
    int getLatestWidth() const { return m_latestWidth; }
    int getLatestHeight() const { return m_latestHeight; }
    int getLatestStrideY() const { return m_latestStrideY; }
    int getLatestStrideUV() const { return m_latestStrideUV; }

    // Update frame buffers from decoder (thread-safe)
    void updateFrameBuffer(
        const std::vector<uint8_t>& yData,
        const std::vector<uint8_t>& uData,
        const std::vector<uint8_t>& vData,
        int width,
        int height
    );

    // ============================================================================
    // User Profile
    // ============================================================================
    
    struct UserProfile {
        std::string userId;
        std::string profileId;
        std::string displayName;
        std::string avatarUrl;
    };
    
    const UserProfile& getUserProfile() const { return m_userProfile; }
    void setUserProfile(const UserProfile& profile);
    bool hasUserProfile() const { return !m_userProfile.userId.empty(); }

private:
    GameState();
    
    // Thread-safe state access with mutex
    mutable std::mutex m_mutex;
    
    // Session state
    SessionState m_sessionState = SessionState::Idle;
    ViewState m_currentView = ViewState::LoginCBL;
    
    // Session info
    GameSessionInfo m_sessionInfo;
    
    // Streaming metrics (atomic for lock-free access)
    std::atomic<bool> m_isStreaming{false};
    std::atomic<uint64_t> m_latency{0};
    std::atomic<uint64_t> m_bitrate{0};
    
    // UI state
    int m_mediaViewerIdx = 0;
    std::vector<std::string> m_favoriteIds;
    
    // Frame buffers (zero-copy access for rendering)
    mutable std::mutex m_frameBufferMutex;
    std::vector<uint8_t> m_latestFrameY;
    std::vector<uint8_t> m_latestFrameU;
    std::vector<uint8_t> m_latestFrameV;
    int m_latestWidth = 1920;
    int m_latestHeight = 1080;
    int m_latestStrideY = 1920;
    int m_latestStrideUV = 960;
    
    // User profile
    UserProfile m_userProfile;
};

} // namespace luna::game
