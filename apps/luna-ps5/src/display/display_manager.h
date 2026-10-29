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
    std::string videoUrl;

    std::vector<uint8_t> coverRgba;
    int coverWidth = 0;
    int coverHeight = 0;
    std::string pegiRating = "PEGI 12";
    std::vector<std::string> ageRatingDescriptors;
    bool detailsLoaded = false;
    std::string lastPlayedDate;
    std::vector<GameTile> similarGames;
};

struct LunaCollection {
    std::string id;
    std::string title;
    std::string subtitle;
    bool isHeroRotator = false;
    bool isRankings = false;
    bool isPartnerRow = false;
    bool isCategoryCarousel = false;
    bool isRecentlyPlayed16_9 = false;
    std::string partnerPromoImage;
    std::string partnerTitle;
    std::string partnerDescription;
    std::string partnerButtonText = "Ver todos los juegos";
    std::vector<GameTile> items;
};

struct PlayerProfileInfo {
    std::string displayName = "Bornicho";
    std::string discriminator = "#8663";
    std::string planStatus = "Incluido con Prime";
    std::string avatarUrl = "https://m.media-amazon.com/images/G/01/T/TC05316420/A07531864/Avatars/Privates_Poo.png";
    std::string profileId = "amzn1.actor.person.oid.A24FP4VJ7KI5CD";
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
    void drawPreScaledImage(int rx, int ry, const uint8_t* rgba, int dstW, int dstH, int srcW = 0, int srcH = 0);
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
                            int searchZone = 0,
                            int searchLetterIdx = 0,
                            int searchGridIdx = 0,
                            const std::vector<GameTile>& filteredGames = {},
                            bool hoverCardOpen = false);

    void drawGameDetailScreen(const GameTile& game,
                              const PlayerProfileInfo& profile,
                              const std::string& statusText,
                              bool isConnecting,
                              int detailZone,
                              int detailBtnIdx,
                              int similarSelectedIdx,
                              uint64_t frameCount,
                              bool isFavorite = false);

    void drawNativeEngineWaitingScreen(const std::string& localWebUrl,
                                       const std::string& lanWebUrl,
                                       const std::string& statusMsg,
                                       uint64_t frameCount);

    void drawSettingsScreen(int selectedItemIdx,
                            const std::string& statusText,
                            uint64_t frameCount,
                            int accountsSubTab = 0);

    // kind: 0 = Gestionar perfiles, 1 = Planes Luna, 2 = Distintivos, 3 = Stream
    void drawProfileScreen(int kind,
                           int sel,
                           int subTab,
                           const PlayerProfileInfo& profile,
                           const std::vector<GameTile>& games,
                           uint64_t frameCount);

    void setSearchQuery(const std::string& q) { searchQuery_ = q; }
    // The next present() call is swallowed so a modal can be drawn over the current scene in the same frame.
    void deferNextPresent() { deferPresent_ = true; }

    void drawMediaViewerScreen(const GameTile& game,
                               int currentMediaIdx,
                               uint64_t frameCount);

    void drawStartingSession(const GameTile& game,
                             const std::string& statusMsg,
                             uint64_t frameCount,
                             bool cancelModalOpen = false,
                             int cancelModalBtnIdx = 0);

    void drawPostSessionModal(const std::vector<GameTile>& suggestions,
                              int selectedSuggestionIdx,
                              uint64_t frameCount);

    void drawFaqModal(const std::string& faqQuestion,
                      const std::string& faqAnswer,
                      uint64_t frameCount);

    void drawBluetoothGamepadModal(uint64_t frameCount);

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
    void putPixel(int x, int y, uint32_t colorArgb);
    uint32_t getPixel(int x, int y) const;
    void blendPixel(int x, int y, uint32_t colorArgb, uint8_t alpha);

    void setPixelSwizzled(uint8_t*, int x, int y, uint32_t colorArgb) { putPixel(x, y, colorArgb); }
    uint32_t getPixelSwizzled(const uint8_t*, int x, int y) const { return getPixel(x, y); }
    void blendPixelSwizzled(uint8_t*, int x, int y, uint32_t colorArgb, uint8_t alpha) { blendPixel(x, y, colorArgb, alpha); }

    void drawHoverPreviewCard(int anchorX, int anchorY, const GameTile& tile, uint64_t frameCount);

    FontRenderer fontRenderer_;

    int width_ = 1920;
    int height_ = 1080;
    bool isReady_ = false;

    // Linear offscreen frame canvas in system RAM (ABGR word order for PS5/PC)
    std::vector<uint32_t> canvas_;

    int videoHandle_ = -1;
    void* mappedMemory_ = nullptr;
    int currentBuffer_ = 0;
    int64_t flipMarker_ = 0;

    std::vector<uint8_t> heroBannerRgba_;
    int heroBannerWidth_ = 0;
    int heroBannerHeight_ = 0;
    void loadStaticHeroBanner();

    void blendRectUniform(int x, int y, int w, int h, uint32_t colorArgb, uint8_t alpha);
    void applyAlphaMask(const std::vector<uint8_t>& mask, int maskW, int maskH,
                        const std::vector<int>& rowStart, const std::vector<int>& rowEnd,
                        int destX, int destY, uint32_t colorArgb);
    void ensureMasks();

    // Pre-computed render caches (built once, reused every frame)
    std::vector<uint8_t> bgCache_;
    uint32_t bgCacheColor_ = 0;
    std::vector<uint8_t> sceneCache_;
    std::string sceneKey_;
    std::vector<uint8_t> heroMask_;
    std::vector<int> heroRowStart_, heroRowEnd_;
    std::vector<uint8_t> detailMask_;
    std::vector<int> detailRowStart_, detailRowEnd_;
    bool masksReady_ = false;
    float detailScrollY_ = 0.0f;
    std::string searchQuery_;
    bool deferPresent_ = false;
};

} // namespace luna::display
