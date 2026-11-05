#pragma once

#include "display/display_manager.h"
#include <vector>
#include <string>
#include <cstdint>

namespace luna::display {

struct HomeScreenProps {
    const std::vector<LunaCollection>& collections;
    const std::vector<std::string>& navTabs;
    int activeTabIdx = 0;
    int focusZone = 1;
    int navColIdx = 0;
    int selectedRowIdx = 0;
    const std::vector<int>& rowSelectedCol;
    int heroBtnIdx = 0;
    const PlayerProfileInfo& profile;
    bool padConnected = true;
    const std::string& statusText;
    uint64_t frameCount = 0;
    bool profileMenuOpen = false;
    int profileMenuIdx = 0;
    bool searchOpen = false;
    int searchZone = 0;
    int searchLetterIdx = 0;
    int searchGridIdx = 0;
    const std::vector<GameTile>& filteredGames;
    bool hoverCardOpen = false;
    std::string searchQuery;
    const std::vector<std::string>* favoriteIds = nullptr;
};

struct GameDetailProps {
    const GameTile& game;
    const PlayerProfileInfo& profile;
    const std::string& statusText;
    bool isConnecting = false;
    int detailZone = 0;
    int detailBtnIdx = 0;
    int similarSelectedIdx = 0;
    uint64_t frameCount = 0;
    bool isFavorite = false;
};

struct SettingsScreenProps {
    int selectedItemIdx = 0;
    const std::string& statusText;
};

struct ErrorScreenProps {
    const std::string& errorMessage;
    const std::string& errorCode;
    const std::string& statusText;
};

struct LoginScreenProps {
    const std::string& statusText;
    const std::string& pinCode;
    const std::string& countdown;
    bool waiting = true;
};

struct MediaViewerScreenProps {
    const std::string& statusText;
    const std::string& gameTitle;
    const std::string& developer;
    const std::string& currentTime;
    const std::string& totalTime;
    bool isPaused = false;
    bool showControls = true;
};

struct StreamingDashboardScreenProps {
    const std::string& statusText;
    const std::string& gameTitle;
    const std::string& ping;
    const std::string& latency;
    const std::string& bitrate;
    const std::string& quality;
    bool isConnected = true;
    bool showHud = true;
};

struct RemotePlayScreenProps {
    const std::string& statusText;
    const std::vector<LunaCollection>& collections;
    int selectedIdx = 0;
};

class LunaClayUI {
public:
    static void renderHomeScreen(DisplayManager& disp, const HomeScreenProps& props);
    static void renderGameDetail(DisplayManager& disp, const GameDetailProps& props);
    static void renderSettingsScreen(DisplayManager& disp, const SettingsScreenProps& props);
    static void renderErrorScreen(DisplayManager& disp, const ErrorScreenProps& props);
    static void renderLoginScreen(DisplayManager& disp, const LoginScreenProps& props);
    static void renderMediaViewerScreen(DisplayManager& disp, const MediaViewerScreenProps& props);
    static void renderStreamingDashboardScreen(DisplayManager& disp, const StreamingDashboardScreenProps& props);
    static void renderRemotePlayScreen(DisplayManager& disp, const RemotePlayScreenProps& props);
};

} // namespace luna::display
