#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include "display/font_renderer.h"

namespace luna::display {

struct GameTile {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string buttonText = "â¶ Jugar ahora";
    std::string secondaryButtonText = "";
    bool isEntitled = true;
    std::string playToken;
    std::string buildId;

    std::string publisher;
    std::string developer;
    std::string releaseYear;
    std::string genres;
    std::string inputMethods = "Mando DualSense";
    std::string playerCount = "1 jugador";
    std::string metacritic;
    std::string channelSubtext = "Incluido con Prime";
    uint32_t badgeColor = 0xFF9146FF;
    std::string category = "LUNA+";
    std::string synopsis = "Disponible para jugar al instante en Amazon Luna Cloud.";
    uint32_t heroColor = 0xFF9146FF;

    std::string coverUrl;
    std::string backdropUrl;
    std::string logoUrl;
    std::vector<std::string> screenshotUrls;

    std::vector<uint8_t> coverRgba;
    int coverWidth = 0;
    int coverHeight = 0;
    std::string pegiRating = "PEGI 12";
    std::vector<std::string> ageRatingDescriptors;
    bool detailsLoaded = false;
    std::vector<GameTile> similarGames;
};

struct LunaCollection {
    std::string id;
    std::string title;
    std::string subtitle;
    bool isHeroRotator = false;
    bool isRankings = false;
    std::vector<GameTile> items;
};

struct PlayerProfileInfo {
    std::string displayName = "Bornicho";
    std::string discriminator = "#8663";
    std::string planStatus = "Incluido con Prime";
};

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();

    bool init(int width = 1920, int height = 1080);
    void shutdown();

    void clear(uint32_t colorArgb);
    void fillRect(int x, int y, int w, int h, uint32_t colorArgb);
    void drawText(int x, int y, int scale, const std::string& text, uint32_t colorArgb);
    void drawQrCode(int x, int y, int moduleScale, const std::string& text);
    void drawImage(int x, int y, int w, int h, const uint8_t* rgba, int imgW, int imgH);
    void drawPreScaledImage(int rx, int ry, const uint8_t* rgba, int imgW, int imgH, int strideW = 0);
    void drawRoundedRect(int x, int y, int w, int h, int r, uint32_t colorArgb);
    void drawRoundedBorder(int x, int y, int w, int h, int r, int thickness, uint32_t colorArgb);
    void drawPlayTriangleIcon(int cx, int cy, int size, uint32_t color);
    void drawStarIcon(int cx, int cy, int size, uint32_t color);
    void drawLunaLogoIcon(int x, int y, int size);
    void drawPillButton(int x, int y, int w, int h, const std::string& text, bool focused, const std::string& subtext = "", bool hasPlayIcon = false);

    void drawLoginScreen(const std::string& publicCode,
                         const std::string& verificationUri,
                         int qrSize,
                         const std::function<bool(int, int)>& qrModuleLookup,
                         const std::string& statusText,
                         uint64_t frameCount);

    void drawCatalogScreen(const std::vector<GameTile>& games,
                           int selectedIndex,
                           const std::string& customerId,
                           const std::string& statusText,
                           uint64_t frameCount,
                           const std::string& currentCategory = "TODOS LOS JUEGOS");

    void drawLunaHomeScreen(const std::vector<LunaCollection>& collections,
                            const std::vector<std::string>& navTabs,
                            int activeTabIdx,
                            int focusZone,
                            int navColIdx,
                            int selectedRowIdx,
                            const std::vector<int>& rowSelectedCol,
                            int heroBtnIdx,
                            const PlayerProfileInfo& profile,
                            bool padConnected,
                            const std::string& statusText,
                            uint64_t frameCount,
                            bool profileMenuOpen = false,
                            int profileMenuIdx = 0,
                            bool searchOpen = false,
                            int searchLetterIdx = 0,
                            int searchGridIdx = 0,
                            const std::vector<GameTile>& filteredGames = {});

    void drawGameDetailScreen(const GameTile& game,
                              const PlayerProfileInfo& profile,
                              const std::string& statusText,
                              bool isConnecting,
                              int detailZone,
                              int detailBtnIdx,
                              int similarSelectedIdx,
                              uint64_t frameCount);

    void drawStreamingStatusScreen(const GameTile& game,
                                   const std::string& statusText,
                                   const std::string& statsText,
                                   const std::string& iceStatsText,
                                   uint64_t frameCount);

    void drawDashboard(const std::string& consoleIp,
                       int authPort,
                       const std::string& serverStatus,
                       bool padConnected,
                       uint32_t padButtons,
                       int16_t stickX,
                       int16_t stickY,
                       const std::string& statusText,
                       uint64_t frameCount);

    bool presentYuv420(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                       int strideY, int strideU, int strideV,
                       int width, int height);
    bool presentYuv420WithHud(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                              int strideY, int strideU, int strideV,
                              int width, int height,
                              bool showHud,
                              const std::string& hudLine1,
                              const std::string& hudLine2 = "",
                              bool overlayOpen = false,
                              int overlaySelectedIdx = 0,
                              bool hudPinned = false,
                              const std::string& gameTitle = "");
    void present();

    bool isReady() const { return isReady_; }

private:
    void setPixelSwizzled(uint8_t* base, int x, int y, uint32_t colorArgb);
    uint32_t getPixelSwizzled(const uint8_t* base, int x, int y) const;
    void blendPixelSwizzled(uint8_t* base, int x, int y, uint32_t colorArgb, uint8_t alpha);

    FontRenderer fontRenderer_;

    int width_ = 1920;
    int height_ = 1080;
    bool isReady_ = false;

    int videoHandle_ = -1;
    void* mappedMemory_ = nullptr;
    int currentBuffer_ = 0;
    int64_t flipMarker_ = 0;
};

} // namespace luna::display
