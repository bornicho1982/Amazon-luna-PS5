#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <functional>
#include "display/font_renderer.h"
#include "../session/isession_observer.h"
#include "../session/luna_session.h"  // Para tipos de luna::session::SessionState y GameSessionInfo

namespace luna::display {

struct GameTile;
struct LunaCollection;
struct PlayerProfileInfo;

class DisplayManager : public luna::session::ISessionObserver {
public:
    DisplayManager();
    ~DisplayManager();

    bool init(int width = 1920, int height = 1080);
    void shutdown();

    void clear(uint32_t colorArgb);
    void fillRect(int x, int y, int w, int h, uint32_t colorArgb);
    void drawText(int x, int y, int scale, const std::string& text, uint32_t colorArgb);
    void drawTextPx(int x, int y, int fontSizePx, const std::string& text, uint32_t colorArgb);
    void drawQrCode(int x, int y, int moduleScale, const std::string& text);
    void drawImage(int x, int y, int w, int h, const uint8_t* rgba, int imgW, int imgH);
    void drawPreScaledImage(int rx, int ry, const uint8_t* rgba, int dstW, int dstH, int srcW = 0, int srcH = 0);
    void drawRoundedRect(int x, int y, int w, int h, int r, uint32_t colorArgb);
    void drawRoundedBorder(int x, int y, int w, int h, int r, int thickness, uint32_t colorArgb);
    void drawPlayTriangleIcon(int cx, int cy, int size, uint32_t color);

    struct ClipRect {
        int x0 = 0;
        int y0 = 0;
        int x1 = 1920;
        int y1 = 1080;
    };
    void pushScissor(int x, int y, int w, int h);
    void popScissor();
    void resetScissor();
    const ClipRect& getClipRect() const { return clipRect_; }

    FontRenderer& getFontRenderer() { return fontRenderer_; }
    void drawStarIcon(int cx, int cy, int size, uint32_t color);
    void drawLunaLogoIcon(int x, int y, int size);
    void drawMutedSpeakerIcon(int cx, int cy, int size, uint32_t color);
    void drawCategoryIcon(int cx, int cy, int size, const std::string& catName, uint32_t color);
    void drawCategoryIconTinted(int dx, int dy, const uint8_t* rgba, int dstW, int dstH, int srcW, int srcH, uint32_t color);
    void drawPegiBadge(int x, int y, int w, int h, const std::string& rating);
    void drawHoverPreviewCard(int anchorX, int anchorY, const GameTile& tile, uint64_t frameCount);
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
                            bool hoverCardOpen = false,
                            const std::vector<std::string>* favoriteIds = nullptr);

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

    void drawSettingsScreenClay(int selectedItemIdx,
                                const std::string& statusText);

    void drawErrorScreenClay(const std::string& errorMessage,
                             const std::string& errorCode,
                             const std::string& statusText);

    // kind: 0 = Gestionar perfiles, 1 = Planes Luna, 2 = Distintivos, 3 = Stream
    void drawProfileScreen(int kind,
                           int sel,
                           int subTab,
                           const PlayerProfileInfo& profile,
                           const std::vector<GameTile>& games,
                           uint64_t frameCount);

    void setSearchQuery(const std::string& q) { searchQuery_ = q; }
    const std::string& getSearchQuery() const { return searchQuery_; }
    // The next present() call is swallowed so a modal can be drawn over the current scene in the same frame.
    void deferNextPresent() { deferPresent_ = true; }

    const uint8_t* getHeroBannerRgba() const { return heroBannerRgba_.empty() ? nullptr : heroBannerRgba_.data(); }
    int getHeroBannerWidth() const { return heroBannerWidth_; }
    int getHeroBannerHeight() const { return heroBannerHeight_; }

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

    void putPixel(int x, int y, uint32_t colorArgb);
    uint32_t getPixel(int x, int y) const;
    void blendPixel(int x, int y, uint32_t colorArgb, uint8_t alpha);
    void blendRectUniform(int x, int y, int w, int h, uint32_t colorArgb, uint8_t alpha);

    bool isReady() const { return isReady_; }

public:
    // Singleton access - public for testing and external use
    static DisplayManager* instance();

private:
    // Observer pattern implementation - must be public to override base class methods
    void onSessionStateChanged(session::SessionState oldState, session::SessionState newState) override;
    void onSessionInfoReady(const session::GameSessionInfo& info) override;
    void onCollectionsUpdated(
        const std::vector<luna::display::GameTile>& home,
        const std::vector<luna::display::GameTile>& library
    ) override;
    void onAuthStateChanged(bool authenticated) override;
    void onError(int errorCode, const std::string& message) override;
    void onStreamingStarted(const luna::display::GameTile& game) override;
    void onStreamingStopped() override;

    // UI update methods
    void updateSessionUI(const session::GameSessionInfo& info);
    void updateCatalogUI(
        const std::vector<luna::display::GameTile>& home,
        const std::vector<luna::display::GameTile>& library
    );
    void updateAuthUI(bool authenticated);
    void showErrorUI(int errorCode, const std::string& message);

    // LunaSessionClient observer registration
    session::LunaSessionClient* m_sessionClient = nullptr;  // Observer reference to LunaSessionSessionClient
    
    // Internal state management based on session events
    bool m_sessionReady = false;
    bool m_catalogUpdated = false;
    void setPixelSwizzled(uint8_t*, int x, int y, uint32_t colorArgb) { putPixel(x, y, colorArgb); }
    uint32_t getPixelSwizzled(const uint8_t*, int x, int y) const { return getPixel(x, y); }
    void blendPixelSwizzled(uint8_t*, int x, int y, uint32_t colorArgb, uint8_t alpha) { blendPixel(x, y, colorArgb, alpha); }

    FontRenderer fontRenderer_;

    int width_ = 1920;
    int height_ = 1080;
    bool isReady_ = false;

    ClipRect clipRect_{0, 0, 1920, 1080};
    std::vector<ClipRect> clipStack_;

    // Linear offscreen frame canvas in system RAM (ABGR word order for PS5/PC)
    uint32_t* canvas_ = nullptr;

    int videoHandle_ = -1;
    void* mappedMemory_ = nullptr;
    int currentBuffer_ = 0;
    int64_t flipMarker_ = 0;

    std::vector<uint8_t> heroBannerRgba_;
    int heroBannerWidth_ = 0;
    int heroBannerHeight_ = 0;
    void loadStaticHeroBanner();

    float detailScrollY_ = 0.0f;
    std::string searchQuery_;
    bool deferPresent_ = false;
};

} // namespace luna::display
