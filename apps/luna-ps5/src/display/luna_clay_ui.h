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

class LunaClayUI {
public:
    static void renderHomeScreen(DisplayManager& disp, const HomeScreenProps& props);
    static void renderGameDetail(DisplayManager& disp, const GameDetailProps& props);
};

} // namespace luna::display
