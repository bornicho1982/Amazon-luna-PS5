#include "game/game_loop.h"
#include "display/display_manager.h"
#include "session/luna_session.h"
#include "util/timer.h"
#include <algorithm>
#include <cmath>

namespace luna::game {

// ============================================================================
// CONSTANTES Y CONFIGURACIÓN
// ============================================================================

constexpr float NAV_THRESHOLD_MS = 120.0f;
constexpr int CATALOG_PAGE_SIZE = 4;
constexpr int LIBRARY_PAGE_SIZE = 4;
constexpr int HERO_CONTENTS = 6;

// ============================================================================
// CONSTRUCTORES Y DESTRUCTOR
// ============================================================================

GameLoop::GameLoop() 
    : m_running(false),
      m_catalogView(nullptr),
      m_libraryView(nullptr),
      m_loginView(nullptr),
      m_mediaViewerView(nullptr),
      m_postSessionModalView(nullptr),
      m_settingsView(nullptr),
      m_profileScreenView(nullptr),
      m_streamingView(nullptr),
      m_startingSessionView(nullptr),
      m_searchOpen(false),
      m_searchZone(SearchZone::Home),
      m_searchLetterIdx(0),
      m_searchGridIdx(0),
      m_favoriteIds({}),
      m_frameCount(0) {

    // Inicializar views con estados predeterminados
    m_catalogView = std::make_unique<CatalogView>();
    m_libraryView = std::make_unique<LibraryView>();
    m_loginView = std::make_unique<LoginView>();
    m_mediaViewerView = std::make_unique<MediaViewerView>();
    m_postSessionModalView = std::make_unique<PostSessionModalView>();
    m_settingsView = std::make_unique<SettingsView>();
    m_profileScreenView = std::make_unique<ProfileScreenView>();
    m_streamingView = std::make_unique<StreamingView>();
    m_startingSessionView = std::make_unique<StartingSessionView>();

    // Inicializar estados predeterminados
    setCurrentView(ViewType::Catalog);
}

GameLoop::~GameLoop() {
    stop();
    
    // Limpiar vistas
    m_catalogView.reset();
    m_libraryView.reset();
    m_loginView.reset();
    m_mediaViewerView.reset();
    m_postSessionModalView.reset();
    m_settingsView.reset();
    m_profileScreenView.reset();
    m_streamingView.reset();
    m_startingSessionView.reset();
}

// ============================================================================
// START/STOP
// ============================================================================

void GameLoop::start() {
    m_running = true;
    GameState::instance().setState(GameStateType::Running);
    
    // Inicializar primera vista según estado de sesión
    if (GameState::instance().getState() == GameStateType::Authenticated) {
        setCurrentView(ViewType::Catalog);
    } else {
        setCurrentView(ViewType::Login);
    }
}

void GameLoop::stop() {
    m_running = false;
    
    // Limpiar vistas
    m_catalogView.reset();
    m_libraryView.reset();
    m_loginView.reset();
    m_mediaViewerView.reset();
    m_postSessionModalView.reset();
    m_settingsView.reset();
    m_profileScreenView.reset();
    m_streamingView.reset();
    m_startingSessionView.reset();
    
    GameState::instance().setState(GameStateType::Stopped);
}

// ============================================================================
// INPUT PROCESSING
// ============================================================================

void GameLoop::processInput() {
    if (!m_running) return;
    
    auto padState = luna::input::getPadState();
    auto pressedNow = padState.buttons & 0xFF;
    auto held = padState.leftY < -13000;
    
    m_frameCount++;
    
    // Procesar input según vista actual
    switch (m_currentView) {
        case ViewType::Catalog:
            processCatalogInput(padState, pressedNow);
            break;
            
        case ViewType::Library:
            processLibraryInput(padState, pressedNow);
            break;
            
        case ViewType::Login:
            processLoginInput(padState, pressedNow);
            break;
            
        case ViewType::MediaViewer:
            processMediaViewerInput(padState, pressedNow);
            break;
            
        case ViewType::PostSessionModal:
            processPostSessionModalInput(padState, pressedNow);
            break;
            
        case ViewType::Settings:
            processSettingsInput(padState, pressedNow);
            break;
            
        case ViewType::ProfileScreen:
            processProfileScreenInput(padState, pressedNow);
            break;
            
        case ViewType::Streaming:
            processStreamingInput(padState, pressedNow);
            break;
            
        case ViewType::StartingSession:
            processStartingSessionInput(padState, pressedNow);
            break;
            
        default:
            break;
    }
}

// ============================================================================
// CATALOG VIEW
// ============================================================================

void GameLoop::processCatalogInput(const luna::input::PadState& padState, uint8_t pressedNow) {
    const auto elapsedMs = Timer::now() - m_catalogView->getNavStart();
    
    // Navegación principal
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = (padState.buttons & luna::input::PAD_UP) || held && padState.leftY < -13000;
        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || held && padState.leftY > 13000;
        
        if (moveUp) {
            m_catalogView->navigateUp();
        } else if (moveDown) {
            m_catalogView->navigateDown();
        }
        
        m_catalogView->setNavStart(Timer::now());
    }
    
    // Selección
    if (pressedNow & luna::input::PAD_CROSS) {
        m_catalogView->selectItem();
    }
    
    // Botones de acción
    if (pressedNow & luna::input::PAD_SQUARE) {
        m_catalogView->showSearch(true);
    }
    
    if (pressedNow & luna::input::PAD_CIRCLE) {
        m_catalogView->showSearch(false);
        // Ir a la biblioteca
        changeView(ViewType::Library);
    }
    
    if (pressedNow & luna::input::PAD_TRIANGLE) {
        m_catalogView->showDetails();
    }
    
    // Buscar
    if (pressedNow & luna::input::PAD_SQUARE && m_searchOpen) {
        m_searchOpen = false;
        // Cerrar búsqueda y volver al catálogo
    }
}

// ============================================================================
// LIBRARY VIEW
// ============================================================================

void GameLoop::processLibraryInput(const luna::input::PadState& padState, uint8_t pressedNow) {
    const auto elapsedMs = Timer::now() - m_libraryView->getNavStart();
    
    // Navegación principal
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = (padState.buttons & luna::input::PAD_UP) || held && padState.leftY < -13000;
        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || held && padState.leftY > 13000;
        
        if (moveUp) {
            m_libraryView->navigateUp();
        } else if (moveDown) {
            m_libraryView->navigateDown();
        }
        
        m_libraryView->setNavStart(Timer::now());
    }
    
    // Selección
    if (pressedNow & luna::input::PAD_CROSS) {
        m_libraryView->selectItem();
    }
    
    // Botones de acción
    if (pressedNow & luna::input::PAD_SQUARE) {
        m_libraryView->showSearch(true);
    }
    
    if (pressedNow & luna::input::PAD_CIRCLE) {
        m_libraryView->showSearch(false);
        // Ir al catálogo
        changeView(ViewType::Catalog);
    }
}

// ============================================================================
// LOGIN VIEW
// ============================================================================

void GameLoop::processLoginInput(const luna::input::PadState& padState, uint8_t pressedNow) {
    const auto elapsedMs = Timer::now() - m_loginView->getNavStart();
    
    // Navegación entre cuentas
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = (padState.buttons & luna::input::PAD_UP) || held && padState.leftY < -13000;
        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || held && padState.leftY > 13000;
        
        if (moveUp) {
            m_loginView->navigateUp();
        } else if (moveDown) {
            m_loginView->navigateDown();
        }
        
        m_loginView->setNavStart(Timer::now());
    }
    
    // Selección e inicio de sesión
    if (pressedNow & luna::input::PAD_CROSS) {
        m_loginView->selectAccount();
    }
    
    if (pressedNow & luna::input::PAD_TRIANGLE) {
        m_loginView->showSettings();
    }
}

// ============================================================================
// MEDIA VIEWER VIEW
// ============================================================================

void GameLoop::processMediaViewerInput(const luna::input::PadState& padState, uint8_t pressedNow) {
    const auto elapsedMs = Timer::now() - m_mediaViewerView->getNavStart();
    
    // Navegación en media viewer
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = (padState.buttons & luna::input::PAD_UP) || held && padState.leftY < -13000;
        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || held && padState.leftY > 13000;
        
        if (moveUp) {
            m_mediaViewerView->navigateUp();
        } else if (moveDown) {
            m_mediaViewerView->navigateDown();
        }
        
        m_mediaViewerView->setNavStart(Timer::now());
    }
    
    // Selección
    if (pressedNow & luna::input::PAD_CROSS) {
        m_mediaViewerView->selectItem();
    }
    
    // Botones de acción
    if (pressedNow & luna::input::PAD_SQUARE) {
        m_mediaViewerView->showDetails();
    }
    
    if (pressedNow & luna::input::PAD_CIRCLE) {
        m_mediaViewerView->goBack();
        changeView(ViewType::Catalog);
    }
}

// ============================================================================
// POST SESSION MODAL VIEW
// ============================================================================

void GameLoop::processPostSessionModalInput(const luna::input::PadState& padState, uint8_t pressedNow) {
    const auto elapsedMs = Timer::now() - m_postSessionModalView->getNavStart();
    
    // Navegación entre opciones
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = (padState.buttons & luna::input::PAD_UP) || held && padState.leftY < -13000;
        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || held && padState.leftY > 13000;
        
        if (moveUp) {
            m_postSessionModalView->navigateUp();
        } else if (moveDown) {
            m_postSessionModalView->navigateDown();
        }
        
        m_postSessionModalView->setNavStart(Timer::now());
    }
    
    // Selección
    if (pressedNow & luna::input::PAD_CROSS) {
        m_postSessionModalView->selectOption();
    }
}

// ============================================================================
// SETTINGS VIEW
// ============================================================================

void GameLoop::processSettingsInput(const luna::input::PadState& padState, uint8_t pressedNow) {
    const auto elapsedMs = Timer::now() - m_settingsView->getNavStart();
    
    // Navegación en settings
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = (padState.buttons & luna::input::PAD_UP) || held && padState.leftY < -13000;
        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || held && padState.leftY > 13000;
        
        if (moveUp) {
            m_settingsView->navigateUp();
        } else if (moveDown) {
            m_settingsView->navigateDown();
        }
        
        m_settingsView->setNavStart(Timer::now());
    }
    
    // Selección
    if (pressedNow & luna::input::PAD_CROSS) {
        m_settingsView->selectOption();
    }
    
    // Volver atrás
    if (pressedNow & luna::input::PAD_CIRCLE) {
        changeView(ViewType::Catalog);
    }
}

// ============================================================================
// PROFILE SCREEN VIEW
// ============================================================================

void GameLoop::processProfileScreenInput(const luna::input::PadState& padState, uint8_t pressedNow) {
    const auto elapsedMs = Timer::now() - m_profileScreenView->getNavStart();
    
    // Navegación en profile screen
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = (padState.buttons & luna::input::PAD_UP) || held && padState.leftY < -13000;
        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || held && padState.leftY > 13000;
        
        if (moveUp) {
            m_profileScreenView->navigateUp();
        } else if (moveDown) {
            m_profileScreenView->navigateDown();
        }
        
        m_profileScreenView->setNavStart(Timer::now());
    }
    
    // Selección
    if (pressedNow & luna::input::PAD_CROSS) {
        m_profileScreenView->selectItem();
    }
    
    // Volver atrás
    if (pressedNow & luna::input::PAD_CIRCLE) {
        changeView(ViewType::Catalog);
    }
}

// ============================================================================
// STREAMING VIEW
// ============================================================================

void GameLoop::processStreamingInput(const luna::input::PadState& padState, uint8_t pressedNow) {
    const auto elapsedMs = Timer::now() - m_streamingView->getNavStart();
    
    // Navegación en streaming
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = (padState.buttons & luna::input::PAD_UP) || held && padState.leftY < -13000;
        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || held && padState.leftY > 13000;
        
        if (moveUp) {
            m_streamingView->navigateUp();
        } else if (moveDown) {
            m_streamingView->navigateDown();
        }
        
        m_streamingView->setNavStart(Timer::now());
    }
    
    // Selección
    if (pressedNow & luna::input::PAD_CROSS) {
        m_streamingView->selectItem();
    }
    
    // Volver atrás
    if (pressedNow & luna::input::PAD_CIRCLE) {
        changeView(ViewType::Catalog);
    }
}

// ============================================================================
// STARTING SESSION VIEW
// ============================================================================

void GameLoop::processStartingSessionInput(const luna::input::PadState& padState, uint8_t pressedNow) {
    const auto elapsedMs = Timer::now() - m_startingSessionView->getNavStart();
    
    // Navegación en starting session
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = (padState.buttons & luna::input::PAD_UP) || held && padState.leftY < -13000;
        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || held && padState.leftY > 13000;
        
        if (moveUp) {
            m_startingSessionView->navigateUp();
        } else if (moveDown) {
            m_startingSessionView->navigateDown();
        }
        
        m_startingSessionView->setNavStart(Timer::now());
    }
    
    // Selección
    if (pressedNow & luna::input::PAD_CROSS) {
        m_startingSessionView->selectItem();
    }
}

// ============================================================================
// VIEW MANAGEMENT
// ============================================================================

void GameLoop::setCurrentView(ViewType viewType) {
    m_currentView = viewType;
    
    // Resetear nav timers
    if (m_catalogView) m_catalogView->resetNavTimer();
    if (m_libraryView) m_libraryView->resetNavTimer();
    if (m_loginView) m_loginView->resetNavTimer();
    if (m_mediaViewerView) m_mediaViewerView->resetNavTimer();
    if (m_postSessionModalView) m_postSessionModalView->resetNavTimer();
    if (m_settingsView) m_settingsView->resetNavTimer();
    if (m_profileScreenView) m_profileScreenView->resetNavTimer();
    if (m_streamingView) m_streamingView->resetNavTimer();
    if (m_startingSessionView) m_startingSessionView->resetNavTimer();
}

void GameLoop::changeView(ViewType viewType) {
    auto oldView = m_currentView;
    setCurrentView(viewType);
    
    // Notificar transición de vista a observers
    GameState::instance().setLastView(oldView, viewType);
    
    // Manejar lógica específica de transición
    handleViewTransition(oldView, viewType);
}

void GameLoop::handleViewTransition(ViewType fromView, ViewType toView) {
    switch (toView) {
        case ViewType::Catalog:
            if (fromView == ViewType::Library) {
                // Recargar catálogo si es necesario
                loadCatalogIfAuthenticated();
            }
            break;
            
        case ViewType::Library:
            // Cargar biblioteca
            loadLibrary();
            break;
            
        case ViewType::Login:
            // Limpiar estado de sesión anterior
            clearSessionState();
            break;
            
        case ViewType::MediaViewer:
            // Inicializar media viewer
            initMediaViewer();
            break;
            
        case ViewType::PostSessionModal:
            // Mostrar modal post-session
            showPostSessionModal();
            break;
            
        case ViewType::Settings:
            // Cargar settings
            loadSettings();
            break;
            
        case ViewType::ProfileScreen:
            // Cargar profile screen
            loadProfileScreen();
            break;
            
        case ViewType::Streaming:
            // Iniciar streaming
            startStreaming();
            break;
            
        case ViewType::StartingSession:
            // Mostrar starting session
            showStartingSession();
            break;
            
        default:
            break;
    }
}

// ============================================================================
// SESSION EVENT HANDLERS
// ============================================================================

void GameLoop::onSessionStateChanged(SessionState oldState, SessionState newState) {
    GameState::instance().setSessionState(newState);
    
    switch (newState) {
        case SessionState::Idle:
            if (m_currentView == ViewType::Login) {
                // Si estamos en login y la sesión está idle, mostrar modal post-session
                changeView(ViewType::PostSessionModal);
            } else if (m_currentView == ViewType::Catalog && !GameState::instance().isAuthenticated()) {
                // Si perdemos sesión mientras estamos en catálogo, ir al login
                changeView(ViewType::Login);
            }
            break;
            
        case SessionState::Starting:
            changeView(ViewType::StartingSession);
            break;
            
        case SessionState::Running:
            if (m_currentView == ViewType::StartingSession) {
                // Cambiar a catálogo cuando la sesión está corriendo
                changeView(ViewType::Catalog);
            }
            break;
            
        case SessionState::Stopping:
            if (m_currentView == ViewType::MediaViewer) {
                // Volver al catálogo cuando se detiene la sesión
                changeView(ViewType::PostSessionModal);
            }
            break;
            
        default:
            break;
    }
}

void GameLoop::onSessionInfoReady(const session::GameSessionInfo& info) {
    GameState::instance().setSessionInfo(info);
    
    // Actualizar vista actual con nueva información de sesión
    if (m_currentView == ViewType::Catalog) {
        m_catalogView->updateSessionInfo(info);
    } else if (m_currentView == ViewType::Library) {
        m_libraryView->updateSessionInfo(info);
    } else if (m_currentView == ViewType::MediaViewer) {
        m_mediaViewerView->updateSessionInfo(info);
    }
}

void GameLoop::onCollectionsUpdated(
    const std::vector<luna::display::GameTile>& home,
    const std::vector<luna::display::GameTile>& library
) {
    GameState::instance().setCollections(home, library);
    
    // Actualizar vistas de catálogo y biblioteca
    if (m_currentView == ViewType::Catalog) {
        m_catalogView->updateCollections(home);
    } else if (m_currentView == ViewType::Library) {
        m_libraryView->updateCollections(library);
    }
}

// ============================================================================
// VISTA HELPERS
// ============================================================================

void GameLoop::loadCatalogIfAuthenticated() {
    if (!GameState::instance().isAuthenticated()) {
        return;
    }
    
    // Cargar catálogo desde display manager
    auto [home, library] = luna::display::DisplayManager::instance().getCollections();
    
    if (m_catalogView) {
        m_catalogView->updateCollections(home);
    }
}

void GameLoop::loadLibrary() {
    // Cargar biblioteca desde display manager
    auto [home, library] = luna::display::DisplayManager::instance().getCollections();
    
    if (m_libraryView) {
        m_libraryView->updateCollections(library);
    }
}

void GameLoop::initMediaViewer() {
    // Inicializar media viewer con la información de sesión actual
    auto sessionInfo = GameState::instance().getSessionInfo();
    
    if (m_mediaViewerView) {
        m_mediaViewerView->init(sessionInfo);
    }
}

void GameLoop::showPostSessionModal() {
    // Mostrar modal post-session
    if (m_postSessionModalView) {
        m_postSessionModalView->show();
    }
}

void GameLoop::loadSettings() {
    // Cargar settings
    if (m_settingsView) {
        m_settingsView->load();
    }
}

void GameLoop::loadProfileScreen() {
    // Cargar profile screen
    if (m_profileScreenView) {
        m_profileScreenView->load();
    }
}

void GameLoop::startStreaming() {
    // Iniciar streaming
    auto sessionInfo = GameState::instance().getSessionInfo();
    
    if (m_streamingView) {
        m_streamingView->init(sessionInfo);
    }
}

void GameLoop::showStartingSession() {
    // Mostrar starting session
    auto sessionInfo = GameState::instance().getSessionInfo();
    
    if (m_startingSessionView) {
        m_startingSessionView->init(sessionInfo);
    }
}

void GameLoop::clearSessionState() {
    // Limpiar estado de sesión actual
    GameState::instance().clearSessionInfo();
}

// ============================================================================
// SEARCH HANDLERS
// ============================================================================

void GameLoop::openSearch(SearchZone zone) {
    m_searchOpen = true;
    m_searchZone = zone;
    
    // Actualizar vista actual con estado de búsqueda
    switch (m_currentView) {
        case ViewType::Catalog:
            if (m_catalogView) m_catalogView->updateSearch(m_searchLetterIdx, m_searchGridIdx);
            break;
            
        case ViewType::Library:
            if (m_libraryView) m_libraryView->updateSearch(m_searchLetterIdx, m_searchGridIdx);
            break;
            
        default:
            break;
    }
}

void GameLoop::navigateSearch() {
    const auto elapsedMs = Timer::now() - m_searchNavStart;
    
    if (elapsedMs >= NAV_THRESHOLD_MS) {
        bool moveUp = held && padState.leftY < -13000;
        bool moveDown = held && padState.leftY > 13000;
        
        if (moveUp) {
            m_searchLetterIdx--;
            if (m_searchLetterIdx < 0) m_searchLetterIdx = 25;
        } else if (moveDown) {
            m_searchLetterIdx++;
            if (m_searchLetterIdx > 25) m_searchLetterIdx = 0;
        }
        
        m_searchNavStart = Timer::now();
    }
}

void GameLoop::selectSearchItem() {
    // Seleccionar elemento de búsqueda
    m_searchGridIdx = 0; // Resetear grid index
    
    // Actualizar vista actual con nueva selección
    switch (m_currentView) {
        case ViewType::Catalog:
            if (m_catalogView) m_catalogView->updateSearchSelection(m_searchLetterIdx, m_searchGridIdx);
            break;
            
        case ViewType::Library:
            if (m_libraryView) m_libraryView->updateSearchSelection(m_searchLetterIdx, m_searchGridIdx);
            break;
            
        default:
            break;
    }
}

void GameLoop::closeSearch() {
    m_searchOpen = false;
    
    // Actualizar vista actual para cerrar búsqueda
    switch (m_currentView) {
        case ViewType::Catalog:
            if (m_catalogView) m_catalogView->updateSearch(false, 0, 0);
            break;
            
        case ViewType::Library:
            if (m_libraryView) m_libraryView->updateSearch(false, 0, 0);
            break;
            
        default:
            break;
    }
}

// ============================================================================
// FAVORITES HANDLERS
// ============================================================================

void GameLoop::toggleFavorite(const std::string& gameId) {
    // Toggle favorite en la lista de favoritos
    auto it = std::find(m_favoriteIds.begin(), m_favoriteIds.end(), gameId);
    
    if (it != m_favoriteIds.end()) {
        // Eliminar de favoritos
        m_favoriteIds.erase(it);
    } else {
        // Agregar a favoritos
        m_favoriteIds.push_back(gameId);
    }
    
    // Actualizar vista actual
    switch (m_currentView) {
        case ViewType::Catalog:
            if (m_catalogView) m_catalogView->updateFavorites(m_favoriteIds);
            break;
            
        case ViewType::Library:
            if (m_libraryView) m_libraryView->updateFavorites(m_favoriteIds);
            break;
            
        default:
            break;
    }
}

void GameLoop::openFavorites() {
    // Abrir vista de favoritos
    changeView(ViewType::Catalog);
    
    if (m_catalogView) {
        m_catalogView->showFavorites();
    }
}

// ============================================================================
// DETAIL VIEW HANDLERS
// ============================================================================

void GameLoop::openGameDetail(const std::string& gameId) {
    // Abrir detalle de juego
    auto sessionInfo = GameState::instance().getSessionInfo();
    
    // Buscar juego en home o library
    auto [home, library] = GameState::instance().getCollections();
    
    std::string currentDetailGame;
    for (const auto& tile : home) {
        if (tile.gameId == gameId) {
            currentDetailGame = tile.title;
            break;
        }
    }
    
    for (const auto& tile : library) {
        if (tile.gameId == gameId) {
            currentDetailGame = tile.title;
            break;
        }
    }
    
    // Mostrar detalle del juego
    if (m_catalogView) {
        m_catalogView->showGameDetail(gameId, currentDetailGame);
    }
}

void GameLoop::requestTileImages(const std::vector<luna::display::GameTile>& tiles) {
    // Solicitar imágenes para tiles
    for (const auto& tile : tiles) {
        luna::display::DisplayManager::instance().requestTileImage(tile.gameId);
    }
}

// ============================================================================
// FRAME COUNTING
// ============================================================================

int GameLoop::getFrameCount() const {
    return m_frameCount;
}

// ============================================================================
// GETTER/SETTER HELPERS
// ============================================================================

bool GameLoop::isSearchOpen() const {
    return m_searchOpen;
}

SearchZone GameLoop::getSearchZone() const {
    return m_searchZone;
}

int GameLoop::getSearchLetterIdx() const {
    return m_searchLetterIdx;
}

int GameLoop::getSearchGridIdx() const {
    return m_searchGridIdx;
}

const std::vector<std::string>& GameLoop::getFavoriteIds() const {
    return m_favoriteIds;
}

ViewType GameLoop::getCurrentView() const {
    return m_currentView;
}

bool GameLoop::isRunning() const {
    return m_running;
}

} // namespace luna::game
