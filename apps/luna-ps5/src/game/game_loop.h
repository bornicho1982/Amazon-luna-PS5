#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include "game/game_state.h"
#include "session/isession_observer.h"
#include "input/pad_manager.h"
#include "display/display_manager.h"

namespace luna::game {

/**
 * @brief GameLoop - State machine central que coordina transiciones de vista
 * 
 * Patrón State: maneja transiciones entre vistas (Login → Catalog → MediaViewer, etc.)
 * Reemplaza lógica dispersa en main.cpp (~1000+ líneas)
 */
class GameLoop {
public:
    GameLoop();
    ~GameLoop();
    
    // Start/stop the game loop
    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // Process input and update state (called every frame)
    void processInput();
    
    // Handle session events from LunaSessionClient observers
    void onSessionStateChanged(SessionState oldState, SessionState newState);
    void onSessionInfoReady(const session::GameSessionInfo& info);
    void onCollectionsUpdated(
        const std::vector<luna::display::GameTile>& home,
        const std::vector<luna::display::GameTile>& library
    );

    // Frame timing (60 FPS target)
    void frameStart();
    void frameEnd();
    
    // Get current time delta since last frame
    static double deltaTime() { return m_lastFrameTime ? 
        std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()) -
        *m_lastFrameTime : 0.0; }

    // State accessors
    AppState getCurrentAppState() const { return GameState::instance().getState(); }
    ViewType getCurrentView() const { return m_currentView; }

private:
    // State machine - Vista actual
    enum class ViewType {
        Catalog,
        Library,
        Login,
        MediaViewer,
        PostSessionModal,
        Settings,
        ProfileScreen,
        Streaming,
        StartingSession
    };
    
    // Current view state
    ViewType m_currentView;
    
    // Session data
    session::GameSessionInfo m_sessionInfo;
    std::vector<luna::display::GameTile> m_homeTiles;
    std::vector<luna::display::GameTile> m_libraryTiles;

    // UI state
    bool m_searchOpen;
    SearchZone m_searchZone;
    int m_searchLetterIdx;
    int m_searchGridIdx;
    std::vector<std::string> m_favoriteIds;
    int m_frameCount;

    // Navigation timers
    std::chrono::steady_clock::time_point m_navStart;
    
    // Input state
    luna::input::PadState m_padState;
    uint8_t m_pressedNow;
    bool m_held;

    // View managers (view controllers)
    struct CatalogView {
        void updateCollections(const std::vector<luna::display::GameTile>& tiles);
        void selectItem();
        void navigateUp();
        void navigateDown();
        void showSearch(bool open);
        void showDetails();
        void updateSessionInfo(const session::GameSessionInfo& info);
        void updateSearch(int letterIdx, int gridIdx);
        void updateSearchSelection(int letterIdx, int gridIdx);
        void updateSearch(bool open, int letterIdx, int gridIdx);
        void updateFavorites(const std::vector<std::string>& favorites);
        void showFavorites();
        void showGameDetail(const std::string& gameId, const std::string& title);
        void resetNavTimer() { m_navStart = std::chrono::steady_clock::now(); }
        std::chrono::steady_clock::time_point getNavStart() const { return m_navStart; }
        
    private:
        std::chrono::steady_clock::time_point m_navStart;
    };

    struct LibraryView {
        void updateCollections(const std::vector<luna::display::GameTile>& tiles);
        void selectItem();
        void navigateUp();
        void navigateDown();
        void showSearch(bool open);
        void updateSessionInfo(const session::GameSessionInfo& info);
        void updateSearch(int letterIdx, int gridIdx);
        void updateSearchSelection(int letterIdx, int gridIdx);
        void updateSearch(bool open, int letterIdx, int gridIdx);
        void updateFavorites(const std::vector<std::string>& favorites);
        void resetNavTimer() { m_navStart = std::chrono::steady_clock::now(); }
        std::chrono::steady_clock::time_point getNavStart() const { return m_navStart; }
        
    private:
        std::chrono::steady_clock::time_point m_navStart;
    };

    struct LoginView {
        void selectAccount();
        void navigateUp();
        void navigateDown();
        void showSettings();
        void resetNavTimer() { m_navStart = std::chrono::steady_clock::now(); }
        std::chrono::steady_clock::time_point getNavStart() const { return m_navStart; }
        
    private:
        std::chrono::steady_clock::time_point m_navStart;
    };

    struct MediaViewerView {
        void init(const session::GameSessionInfo& info);
        void selectItem();
        void navigateUp();
        void navigateDown();
        void goBack();
        void updateSessionInfo(const session::GameSessionInfo& info);
        void resetNavTimer() { m_navStart = std::chrono::steady_clock::now(); }
        std::chrono::steady_clock::time_point getNavStart() const { return m_navStart; }
    };

    struct PostSessionModalView {
        void show();
        void selectOption();
        void navigateUp();
        void navigateDown();
        void resetNavTimer() { m_navStart = std::chrono::steady_clock::now(); }
        std::chrono::steady_clock::time_point getNavStart() const { return m_navStart; }
    };

    struct SettingsView {
        void load();
        void selectOption();
        void navigateUp();
        void navigateDown();
        void resetNavTimer() { m_navStart = std::chrono::steady_clock::now(); }
        std::chrono::steady_clock::time_point getNavStart() const { return m_navStart; }
    };

    struct ProfileScreenView {
        void load();
        void selectItem();
        void navigateUp();
        void navigateDown();
        void resetNavTimer() { m_navStart = std::chrono::steady_clock::now(); }
        std::chrono::steady_clock::time_point getNavStart() const { return m_navStart; }
    };

    struct StreamingView {
        void init(const session::GameSessionInfo& info);
        void selectItem();
        void navigateUp();
        void navigateDown();
        void resetNavTimer() { m_navStart = std::chrono::steady_clock::now(); }
        std::chrono::steady_clock::time_point getNavStart() const { return m_navStart; }
    };

    struct StartingSessionView {
        void init(const session::GameSessionInfo& info);
        void selectItem();
        void navigateUp();
        void navigateDown();
        void resetNavTimer() { m_navStart = std::chrono::steady_clock::now(); }
        std::chrono::steady_clock::time_point getNavStart() const { return m_navStart; }
    };

    std::unique_ptr<CatalogView> m_catalogView;
    std::unique_ptr<LibraryView> m_libraryView;
    std::unique_ptr<LoginView> m_loginView;
    std::unique_ptr<MediaViewerView> m_mediaViewerView;
    std::unique_ptr<PostSessionModalView> m_postSessionModalView;
    std::unique_ptr<SettingsView> m_settingsView;
    std::unique_ptr<ProfileScreenView> m_profileScreenView;
    std::unique_ptr<StreamingView> m_streamingView;
    std::unique_ptr<StartingSessionView> m_startingSessionView;

    // Helpers for view transitions
    void setCurrentView(ViewType viewType);
    void changeView(ViewType viewType);
    void handleViewTransition(ViewType fromView, ViewType toView);

    // Session helpers
    void loadCatalogIfAuthenticated();
    void clearSessionState();

    // Search helpers
    void openSearch(SearchZone zone);
    void navigateSearch();
    void selectSearchItem();
    void closeSearch();

    // Favorites helpers
    void toggleFavorite(const std::string& gameId);
    void openFavorites();

    // Detail view helpers
    void openGameDetail(const std::string& gameId);
    void requestTileImages(const std::vector<luna::display::GameTile>& tiles);

    // Frame counting
    int getFrameCount() const;

    // Getters
    bool isSearchOpen() const { return m_searchOpen; }
    SearchZone getSearchZone() const { return m_searchZone; }
    int getSearchLetterIdx() const { return m_searchLetterIdx; }
    int getSearchGridIdx() const { return m_searchGridIdx; }
    const std::vector<std::string>& getFavoriteIds() const { return m_favoriteIds; }

    // Observer implementation
    void onSessionStateChanged(SessionState oldState, SessionState newState) override;
    void onSessionInfoReady(const session::GameSessionInfo& info) override;
    void onCollectionsUpdated(
        const std::vector<luna::display::GameTile>& home,
        const std::vector<luna::display::GameTile>& library
    ) override;

    // Input processing helpers per view
    void processCatalogInput(const luna::input::PadState& padState, uint8_t pressedNow);
    void processLibraryInput(const luna::input::PadState& padState, uint8_t pressedNow);
    void processLoginInput(const luna::input::PadState& padState, uint8_t pressedNow);
    void processMediaViewerInput(const luna::input::PadState& padState, uint8_t pressedNow);
    void processPostSessionModalInput(const luna::input::PadState& padState, uint8_t pressedNow);
    void processSettingsInput(const luna::input::PadState& padState, uint8_t pressedNow);
    void processProfileScreenInput(const luna::input::PadState& padState, uint8_t pressedNow);
    void processStreamingInput(const luna::input::PadState& padState, uint8_t pressedNow);
    void processStartingSessionInput(const luna::input::PadState& padState, uint8_t pressedNow);

    // Global input state
    static luna::input::PadState getPadState() {
        return luna::input::getPadState();
    }

    // Running state
    std::atomic<bool> m_running{false};
    
    // Thread safety for catalog updates
    mutable std::mutex m_catalogMutex;
};

} // namespace luna::game
