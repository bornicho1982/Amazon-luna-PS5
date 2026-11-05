#include "game/game_state.h"
#include <algorithm>

namespace luna::game {

GameState& GameState::instance() {
    static GameState instance;
    return instance;
}

GameState::~GameState() {
}

void GameState::setSessionState(SessionState state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionState = state;
}

void GameState::setCurrentView(ViewState view) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentView = view;
}

void GameState::setSessionInfo(const GameSessionInfo& info) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionInfo = info;
}

void GameState::updateStreamingMetrics(uint64_t latency, uint64_t bitrate) {
    updateLatency(latency);
    updateBitrate(bitrate);
}

void GameState::addFavoriteId(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Avoid duplicates
    if (std::find(m_favoriteIds.begin(), m_favoriteIds.end(), id) == m_favoriteIds.end()) {
        m_favoriteIds.push_back(id);
    }
}

void GameState::removeFavoriteId(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_favoriteIds.erase(
        std::remove(m_favoriteIds.begin(), m_favoriteIds.end(), id),
        m_favoriteIds.end()
    );
}

void GameState::setUserProfile(const UserProfile& profile) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_userProfile = profile;
}

void GameState::updateFrameBuffer(
    const std::vector<uint8_t>& yData,
    const std::vector<uint8_t>& uData,
    const std::vector<uint8_t>& vData,
    int width,
    int height
) {
    std::lock_guard<std::mutex> lock(m_frameBufferMutex);
    
    // Resize if dimensions changed
    if (m_latestFrameY.size() != yData.size()) {
        m_latestFrameY.resize(yData.size());
        m_latestFrameU.resize(uData.size());
        m_latestFrameV.resize(vData.size());
    }
    
    // Zero-copy update
    std::memcpy(m_latestFrameY.data(), yData.data(), yData.size());
    std::memcpy(m_latestFrameU.data(), uData.data(), uData.size());
    std::memcpy(m_latestFrameV.data(), vData.data(), vData.size());
    
    m_latestWidth = width;
    m_latestHeight = height;
    m_latestStrideY = width;
    m_latestStrideUV = width / 2;
}

} // namespace luna::game
