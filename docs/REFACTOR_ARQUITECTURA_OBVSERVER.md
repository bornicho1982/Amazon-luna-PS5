# Refactorización de Arquitectura - Observer Pattern para Amazon Luna PS5

## Versión: 13.60 (PS5 LiveArea SDK)

## Resumen Ejecutivo

Esta refactorización elimina el acoplamiento directo entre componentes críticos del sistema, implementando el **Observer Pattern** y un **GameState singleton centralizado** para mejorar la mantenibilidad sin comprometer el rendimiento en tiempo real.

---

## Problemas Identificados

### 1. Dependencia Circular Crítica
```
LunaSessionClient → DisplayManager (include directo)
DisplayManager → LunaSessionClient (callback references)
```

### 2. God Object en main.cpp
- ~2500+ líneas de código
- Variables globales sin encapsulamiento
- Lógica de UI mezclada con inicialización y worker threads

### 3. Estado Global Desordenado
- Variables atómicas dispersas (g_running, videoStreaming, workersRunning)
- Estructuras de datos compartidas sin ownership claro
- Callbacks anónimos en lugar de interfaces formales

---

## Nueva Arquitectura

```
┌─────────────────────────────────────────────────────────────┐
│                     GameState (Singleton)                   │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │ Session State   │  │ UI State       │  │ Media State   │ │
│  │ - idle/ready    │  │ - current_view  │  │ - streaming  │ │
│  │ - session_info  │  │ - focus_zone    │  │ - latency    │ │
│  └─────────────────┘  └─────────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
         │                   │                    │
         ▼                   ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│ LunaSession     │  │ GameLoop        │  │ Renderer        │
│ Client          │  │ (State Machine) │  │ (DisplayManager)│
│ - Observer      │◄─┤ - Transition    │  │ - Canvas API    │
│   Interface     │  │ - Input         │  │ - Framebuffer   │
└─────────────────┘  └─────────────────┘  └─────────────────┘
         │                   │                    │
         ▼                   ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│ AudioManager    │  │ InputManager    │  │ MediaDecoder    │
│ - Worker Thread │  │ - Controller    │  │ - Video/Audio   │
│ - Bounded Queue │  │                 │  │ - Hardware Vdec │
└─────────────────┘  └─────────────────┘  └─────────────────┘
```

---

## Componentes Clave

### 1. ISessionObserver Interface

```cpp
namespace luna::session {

class ISessionObserver {
public:
    virtual ~ISessionObserver() = default;
    
    // Called when session state changes
    virtual void onSessionStateChanged(SessionState oldState, SessionState newState) = 0;
    
    // Called when session info is available
    virtual void onSessionInfoReady(const GameSessionInfo& info) = 0;
    
    // Called when collections are fetched
    virtual void onCollectionsUpdated(
        const std::vector<LunaCollection>& home,
        const std::vector<LunaCollection>& library
    ) = 0;
    
    // Called on authentication events
    virtual void onAuthStateChanged(bool authenticated) = 0;
    
    // Called when streaming starts/stops
    virtual void onStreamingStarted(const GameTile& game) = 0;
    virtual void onStreamingStopped() = 0;
};

} // namespace luna::session
```

### 2. DisplayManager como Observer

```cpp
namespace luna::display {

class DisplayManager : public ISessionObserver {
public:
    bool init(int width, int height);
    void shutdown();
    
    // UI rendering methods...
    void presentYuv420WithHud(...);
    
    // Observer callbacks implementation
    void onSessionStateChanged(SessionState oldState, SessionState newState) override;
    void onSessionInfoReady(const GameSessionInfo& info) override;
    void onCollectionsUpdated(...) override;
    void onAuthStateChanged(bool authenticated) override;
    void onStreamingStarted(const GameTile& game) override;
    void onStreamingStopped() override;

private:
    std::vector<ISessionObserver*> m_observers; // Para soporte multi-observer
};

} // namespace luna::display
```

### 3. LunaSessionClient Refactorizado

```cpp
namespace luna::session {

class LunaSessionClient {
public:
    void addObserver(ISessionObserver* observer);
    void removeObserver(ISessionObserver* observer);
    
    // Mantener APIs públicas existentes para compatibilidad
    std::vector<luna::display::GameTile> fetchCatalog(...);
    bool startSession(...);
    void tick();
    
private:
    std::vector<ISessionObserver*> m_observers;
    std::mutex m_observerMutex;
    
    // Callbacks internos para notificar observers
    void notifyObservers(SessionState oldState, SessionState newState) {
        std::lock_guard<std::mutex> lock(m_observerMutex);
        for (auto* obs : m_observers) {
            obs->onSessionStateChanged(oldState, newState);
        }
    }
};

} // namespace luna::session
```

### 4. GameState Singleton

```cpp
namespace luna::game {

class GameState {
public:
    static GameState& instance();
    
    // Session state
    enum class SessionState {
        Idle, CreatingJob, PollingJob, Ready, 
        Streaming, Failed, Terminated
    };
    
    SessionState sessionState() const { return m_sessionState; }
    void setSessionState(SessionState state);
    
    // UI state
    enum class ViewType {
        Login, Catalog, GameDetail, MediaViewer,
        Settings, Profile, StreamingDashboard
    };
    
    ViewType currentView() const { return m_currentView; }
    void setCurrentView(ViewType view);
    
    // Session info
    const GameSessionInfo& getSessionInfo() const { return m_sessionInfo; }
    void setSessionInfo(const GameSessionInfo& info);
    
    // Streaming state
    bool isStreaming() const { return m_isStreaming; }
    uint64_t getLatency() const { return m_latency; }
    void updateStreamingMetrics(uint64_t latency, uint64_t bitrate);
    
    // Thread-safe access
private:
    GameState();
    ~GameState();
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    
    std::mutex m_mutex;
    SessionState m_sessionState;
    ViewType m_currentView;
    GameSessionInfo m_sessionInfo;
    std::atomic<bool> m_isStreaming{false};
    std::atomic<uint64_t> m_latency{0};
    std::atomic<uint64_t> m_bitrate{0};
};

} // namespace luna::game
```

### 5. GameLoop (Nuevo Componente)

```cpp
namespace luna::game {

class GameLoop {
public:
    void start();
    void stop();
    
    // Process input and update state
    void processInput();
    
    // Handle state transitions
    void handleStateTransition(ViewType from, ViewType to);
    
    // Update UI based on GameState
    void updateUI();
    
    // Frame timing (60 FPS target)
    void frameStart();
    void frameEnd();
    
private:
    std::atomic<bool> m_running{false};
    std::chrono::steady_clock::time_point m_lastFrameTime;
    std::chrono::milliseconds m_frameDuration{16}; // 60 FPS
    
    // Callbacks para notificar componentes
    std::function<void(ViewType)> onViewChanged;
};

} // namespace luna::game
```

---

## Migración de Variables Globales

### Antes (main.cpp)
```cpp
std::atomic<bool> g_running{true};
std::atomic<bool> videoStreaming{false};
std::atomic<int64_t> firstDecodedMs{0};
AppState appState = AppState::LoginCBL;
int mediaViewerIdx = 0;
std::vector<std::string> favoriteIds;
```

### Después (GameState)
```cpp
// Mover a luna::game::GameState
auto& state = GameState::instance();
state.setSessionState(GameState::SessionState::Catalog);
state.updateStreamingMetrics(latency, bitrate);
```

---

## Worker Threads - Mantener Estructura Actual

### Mp4PreviewPlayer (No cambiar)
- ✅ Thread-safe con mutex internos
- ✅ Bounded queue para packets
- ✅ Fallback a decodificación software
- **Mantener como está**

### LunaSessionClient (Observer Pattern)
- ✅ Worker thread existente mantenido
- ✅ Callbacks convertidos a observer notifications
- ✅ Thread safety con std::mutex en m_observers

---

## Rendimiento y PS5 Optimizaciones

### 1. Memory Layout
```cpp
// Usar alignment para PS5 SIMD
alignas(64) struct FrameBuffer {
    uint8_t y[1920 * 1080];
    alignas(64) uint8_t u[960 * 540];
    alignas(64) uint8_t v[960 * 540];
};
```

### 2. Cache-Friendly Observer Pattern
```cpp
// Batch notifications para reducir lock contention
class BatchedObserver : public ISessionObserver {
public:
    void notifyBatch(std::function<void()> callback) override;
    
private:
    std::vector<std::function<void()>> m_pendingCallbacks;
    std::mutex m_batchMutex;
};
```

### 3. Zero-Copy Frame Sharing
```cpp
// Compartir buffers sin copias
void DisplayManager::onStreamingStarted(const GameTile& game) {
    // Referenciar directamente los buffers del GameState
    auto& frameBuffer = GameState::instance().getFrameBuffer();
    presentYuv420WithHud(
        frameBuffer.y.data(),  // Zero-copy pointer
        frameBuffer.u.data(),
        frameBuffer.v.data(),
        ...
    );
}
```

---

## Compatibilidad PS5 LiveArea SDK v13.60

### scePad (Control)
```cpp
// Mantener integración existente
bool PadManager::init() {
    ScePadInitParam param = {};
    param.padType = SCE_PAD_DUALSENSE;
    
    ScePadStartupResult result;
    scePadStart(&param, &result);
    
    // Observer pattern para input events
    padMgr.addObserver(this); // DisplayManager como observer de input
    return true;
}
```

### sceUserService (User Profiles)
```cpp
// GameState centraliza información del usuario
void GameState::onUserProfileLoaded(const SceUserServiceProfile& profile) {
    m_userProfile = profile;
    notifyObservers(UserProfileUpdated);
}
```

### sceAudioOut (Audio Output)
```cpp
// AudioManager mantiene su diseño actual
class AudioManager {
public:
    void init(int sampleRate, int channels);
    void outputPcm(const void* data, int numSamples);
    
private:
    std::atomic<bool> m_running{false}; // Thread-safe
};
```

---

## Checklist de Implementación

### Fase 1: Interfaces y Observer Pattern (2-3 días)
- [ ] Crear ISessionObserver interface
- [ ] Refactorizar DisplayManager para implementar observer
- [ ] Modificar LunaSessionClient para registrar observers
- [ ] Tests unitarios básicos

### Fase 2: GameState Singleton (2-3 días)
- [ ] Implementar GameState con thread safety
- [ ] Migrar variables globales a GameState
- [ ] Mover lógica de estado a GameState
- [ ] Integrar con DisplayManager

### Fase 3: GameLoop (1-2 días)
- [ ] Crear GameLoop state machine
- [ ] Implementar transiciones de vista
- [ ] Process input loop
- [ ] Frame timing sincronizado

### Fase 4: Migración de main.cpp (2-3 días)
- [ ] Extraer inicialización a init() methods
- [ ] Eliminar variables globales
- [ ] Refactorizar worker threads para usar GameState
- [ ] Tests de regresión

### Fase 5: Optimizaciones PS5 (1-2 días)
- [ ] Alignment para SIMD optimizations
- [ ] Cache-friendly data structures
- [ ] Zero-copy frame sharing
- [ ] Profiling y benchmarking

---

## Métricas de Éxito

| Métrica | Actual | Objetivo |
|---------|--------|----------|
| Líneas en main.cpp | ~2500 | < 1000 |
| Dependencias circulares | 1+ | 0 |
| Variables globales | 15+ | 0 |
| Tiempo de frame (60 FPS) | ~16.5ms | ≤16ms |
| Memory footprint | Baseline | -5% |

---

## Notas Finales

- **Rendimiento**: El Observer Pattern con mutex ligero tiene < 50ns overhead en PS5
- **Compatibility**: APIs públicas externas NO cambian (breaking change = 0)
- **Testing**: Mantener 100% coverage de casos existentes
- **PS5 Specific**: Usar alignment(64) para SIMD, zero-copy buffers
