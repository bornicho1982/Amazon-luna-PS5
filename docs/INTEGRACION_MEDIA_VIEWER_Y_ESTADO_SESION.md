# Documentación de Integración: Mp4PreviewPlayer, DisplayManager y Propagación de Estado

## Resumen Ejecutivo

Esta documentación detalla el flujo completo de integración entre **Mp4PreviewPlayer** (reproductor de trailers), **DisplayManager** (gestión de pantallas UI), y la propagación del estado desde **LunaSessionClient** en la aplicación Amazon Luna PS5 simulada con motor C++ de Antigravity.

---

## 1. Arquitectura General

### Diagrama de Flujo

```
┌─────────────────────────────────────────────────────────────────┐
│                    LunaSessionClient                            │
│  (Estado centralizado: colecciones, perfil, estado sesión)      │
└─────────────────────────────────────────────────────────────────┘
                              │
              Acceso directo (sin observers)
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      main.cpp                                   │
│  (Ciclo principal, gestión de appState)                         │
└─────────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┴────────────────────┐
         ▼                                         ▼
┌──────────────────────┐              ┌─────────────────────────────┐
│   DisplayManager     │              │   LunaSessionClient          │
│  (Pantallas UI)      │◄────────────►│  (Fetch de datos, estado)    │
│                      │              │                             │
│ - drawHomeScreen     │              │ - getHomeCollections()       │
│ - drawGameDetail     │              │ - getGameNightCollections()  │
│ - drawMediaViewer    │              │ - getPlayerProfile()         │
│ - drawHoverPreview   │              │ - getState()                 │
│ - drawStartingSession│              │ - getStatusMessage()         │
└──────────────────────┘              └─────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                   Mp4PreviewPlayer                              │
│  (Reproductor de trailers H.264/AAC con worker thread)          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    AudioManager                                  │
│  (Pre-buffering PCM ~250ms, queue FIFO, grain-based output)     │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Mp4PreviewPlayer - Reproductor de Trailers

### Propósito
Singleton que maneja la reproducción completa de trailers en formato MP4 (H.264 video + AAC audio) con decodificación en tiempo real y controles integrados.

### Componentes Clave

#### Worker Thread Architecture
```cpp
// mp4_preview_player.cpp - workerLoop()
void Mp4PreviewPlayer::workerLoop() {
    while (workerRunning_.load()) {
        // Wait para nuevas solicitudes o continuaciones de rango
        cv_.wait(lk, [this]() { return hasNewRequest_.load() || hasRangeRequest_.load(); });
        
        // Download HTTP Range progresivo con sliding window
        downloadHttpRange(urlToFetch, rangeOffset, isRangeContinuation);
        
        // Parse MP4 atoms (MOOV/MEDIA) y extraer SPS/PPS Annex-B
        parseMp4(dataBuffer);
        
        // Decodificar frames H.264 YUV420 -> RGBA
        decodeVideoPacket(nalUnit);
        
        // Decodificar audio AAC en arena de 128 KB
        decodeAudioPackets();
    }
}
```

#### Bufferes Estáticos Embebidos
| Buffer | Tamaño | Uso |
|--------|--------|-----|
| `g_mp4DownloadBuffer` | 48 MB | Download HTTP progresivo |
| Frame buffer RGBA | 2 MB (960×540) | Video decodificado |
| AAC decoder arena | 128 KB | Decodificación audio |

### Métodos de Control

#### `setVideoUrl(url)`
```cpp
void Mp4PreviewPlayer::setVideoUrl(const std::string& url) {
    // Validación URL
    if (url.empty() || !isHttpUrl(url)) {
        stop();
        return;
    }
    
    // Reset completo del estado
    currentUrl_ = url;
    hasNewRequest_.store(true);
    cancelDownload_.store(true);
    isReady_.store(false);
    hasDecodedFrame_.store(false);
    paused_.store(false);
    
    // Sincronización con AudioManager
    if (audioManager_) {
        audioManager_->clearQueue();  // Limpiar buffer de audio anterior
    }
}
```

#### `togglePause()`
```cpp
void Mp4PreviewPlayer::togglePause() {
    paused_ = !paused_;
    hasNewRequest_.store(true);  // Notificar worker para continuar/pausar
}
```

#### `seekRelative(deltaSamples)`
```cpp
void Mp4PreviewPlayer::seekRelative(int deltaSamples) {
    int64_t newOffset = currentOffset_ + (deltaSamples * kSampleRate / kVideoFrameRate);
    
    // Validación de límites
    if (newOffset < 0 || newOffset > totalDuration_) return;
    
    hasRangeRequest_.store(true);
    hasNewRequest_.store(false);
    cv_.notify_one();
}
```

### Estado Público Exponible
```cpp
bool hasFrame() const              // Frame decodificado disponible
std::string currentUrl() const     // URL actual de video
bool isPaused() const              // Estado pausa/reproducción
int getCurrentSeconds() const      // Tiempo actual en segundos
int getTotalSeconds() const        // Duración total
float getBufferedProgress() const  // Progreso descargado (0.0-1.0)
int getProgress() const            // Progreso de reproducción (segundos)
int getWidth() const               // Resolución width
int getHeight() const              // Resolución height
const uint32_t* getFrameRgba()     // Puntero a frame buffer
```

---

## 3. DisplayManager - Gestión de Pantallas UI

### drawMediaViewerScreen (Línea 1929)

#### Dos Modos de Operación

**Modo 1: Fullscreen Trailer Player** (`currentMediaIdx < 0` o sin screenshots)
```cpp
void DisplayManager::drawMediaViewerScreen(...) {
    // ... configuración inicial ...
    
    if (currentMediaIdx < 0 || gameScreenshots.empty()) {
        // MODO FULLSCREEN - Reproductor de trailer completo
        
        // Renderizar video decodificado
        const uint32_t* vPix = player.hasFrame() ? player.getFrameRgba() : nullptr;
        if (vPix && player.currentUrl() == game.videoUrl) {
            drawPreScaledImage(0, 0, 
                              reinterpret_cast<const uint8_t*>(vPix),
                              1920, 1080, 
                              player.getWidth(), 
                              player.getHeight());
        }
        
        // Controles de reproducción integrados
        drawControlsOverlay();
    } else {
        // MODO GALERÍA - Screenshots del juego
        drawScreenshotGallery(gameScreenshots);
    }
}
```

**Controles Fullscreen Player:**
| Control | Acción |
|---------|--------|
| **Cross (X)** | Toggle pausa/reproducción |
| **L1 + R1** | Rewind/Fast-forward ±5 segundos |
| **Square/Right/Left** | Navegar screenshots (si disponibles) |
| **Circle (O)** | Volver al catálogo + mute |

#### Controles Detallados

```cpp
void DisplayManager::drawControlsOverlay() {
    // Barra de progreso
    int curSec = player.getCurrentSeconds();
    int totSec = player.getTotalSeconds();
    float progressPct = curSec / static_cast<float>(totSec);
    
    // Barra visual (1540, 22, 380px)
    drawRoundedRect(1540, 22, 380, 38, 8, 
                    player.isPaused() ? 0xFFD97706 : 0xFF0070D1);
    
    // Indicador de estado
    std::string stateBadge = player.isPaused() ? "PAUSADO" : "TRÁILER OFICIAL HD (1080p)";
    drawText(stateBadge, 1556, 22);
    
    // Tiempo actual / total
    drawText(toStr(curSec), 1740, 30);
    drawText(toStr(totSec), 1940, 30);
    
    // Barra de progreso detallada con buffering
    int bufW = std::clamp(static_cast<int>(player.getBufferedProgress() * 380), 2, 378);
    drawRoundedRect(1542, 26, bufW, 30, 4, 0xFF555555);
}
```

### drawHoverPreviewCard (Línea 1130)

#### Integración de Video en Tarjetas Hover

```cpp
void DisplayManager::drawHoverPreviewCard(const GameTile& tile, ...) {
    // Mitad superior: Preview de video si disponible
    if (player.hasFrame() && player.currentUrl() == tile.videoUrl) {
        const uint32_t* vPix = player.getFrameRgba();
        drawPreScaledImage(0, 0, 
                          reinterpret_cast<const uint8_t*>(vPix),
                          1920, 1080,
                          player.getWidth(),
                          player.getHeight());
    } else {
        // Fallback: Imagen estática de ImageLoader
        imageLoader.loadAndDrawCover(tile.coverUrl);
    }
    
    // Mitad inferior: Info del juego
    drawGameInfoSection(tile);
    
    // Iconos PEGI y estado muteado
    drawPegiRating();
    if (player.isPaused()) {
        drawMutedIcon();
    }
}
```

---

## 4. Propagación de Estado: LunaSessionClient → DisplayManager

### Patrón de Propagación Directa

La aplicación **NO usa** un patrón de observador explícito. En su lugar, `main.cpp` actúa como mediador central que accede directamente a miembros públicos de `LunaSessionClient`.

#### Flujo de Datos

```
main.cpp (Ciclo Principal)
│
├─► sessionClient.getHomeCollections()      → tabCollections[0]
├─► sessionClient.getGameNightCollections() → tabCollections[1]
├─► sessionClient.getPlayerProfile()         → profile (struct PlayerProfileInfo)
├─► sessionClient.getStatusMessage()         → g_lastStatusText
└─► sessionClient.getState()                 → appState decisions
```

### Ejemplos de Acceso Directo

#### 1. Carga de Colecciones (main.cpp línea 521, 605)

```cpp
// Home tab - Colecciones principales
tabCollections[0] = sessionClient.getHomeCollections();

// Game Night tab - Colecciones temáticas
tabCollections[1] = sessionClient.getGameNightCollections();
```

#### 2. Perfil del Jugador (main.cpp línea 1550, 1591, 1718, 1910)

```cpp
// Home Screen - Header con perfil
display.drawHomeScreen(props);
    // props.profile = sessionClient.getPlayerProfile();

// Game Detail Screen
display.drawGameDetailScreen(
    currentDetailGame, 
    sessionClient.getPlayerProfile(),  // ← Propagación directa
    g_lastStatusText,
    isConnecting,
    ...
);

// Profile Screen
display.drawProfileScreen(
    profileScreenKind,
    profileScreenSel,
    profileSubTab,
    sessionClient.getPlayerProfile(),  // ← Propagación directa
    badgeGames,
    frameCount
);
```

#### 3. Estado de Sesión (main.cpp línea 1918, 1941)

```cpp
case AppState::MediaViewer: {
    if (sessionClient.getState() == luna::session::SessionState::Ready) {
        // Permitir reproducción de trailers
        mediaViewerIdx = -1;
        mediaViewerFromCatalog = true;
        appState = AppState::MediaViewer;
        break;
    } else if (sessionClient.getState() == luna::session::SessionState::Failed) {
        // Manejar errores de sesión
        std::string failMsg = sessionClient.getStatusMessage();
        if (failMsg.find("caducada") != std::string::npos ||
            failMsg.find("403") != std::string::npos) {
            cbl.logout();
            catalogFetched = false;
            appState = AppState::LoginCBL;
        }
        break;
    }
    // ... otros estados ...
}
```

#### 4. Mensajes de Estado (main.cpp línea 762, 914, 1361, 1368)

```cpp
// Actualización de texto de estado en tiempo real
g_lastStatusText = sessionClient.getStatusMessage();

// Uso en múltiples pantallas:
display.drawHomeScreen(...);              // Con g_lastStatusText
display.drawGameDetailScreen(..., g_lastStatusText, ...);
display.drawStartingSession(..., sessionClient.getStatusMessage(), ...);
```

### Estructuras de Datos Propagadas

#### LunaCollection (Colecciones)
```cpp
struct LunaCollection {
    std::string id;
    std::string name;
    std::vector<GameTile> items;  // Juegos en la colección
    
    // Colecciones Home:
    // - Hero Row (1-3 juegos destacados)
    // - Rankings (Top 25 por métrica)
    // - Recently Played (Últimos 10)
    // - New Additions (Nuevos últimos 7 días)
    // - Promos (Ofertas temporales)
    // - Editors Picks (Selección crítica)
    // - Immersive Worlds (Mundos inmersivos)
};
```

#### PlayerProfileInfo (Perfil del Jugador)
```cpp
struct PlayerProfileInfo {
    std::string displayName;
    std::string avatarUrl;
    std::string level;
    std::string joinDate;
    std::string country;
    // ... otros campos de perfil ...
};
```

---

## 5. AudioManager - Sincronización Audio/Video

### Arquitectura de Pre-buffering

#### Configuración Inicial
```cpp
bool AudioManager::init(int sampleRate, int channels) {
    sampleRate_ = sampleRate;      // 48000 Hz (estándar video)
    channels_ = channels;          // 2 (stereo)
    
    // Iniciar thread de audio
    audioThread_ = std::thread(&AudioManager::audioLoop, this);
    
    isReady_.store(true);
    return true;
}
```

#### Queue FIFO con Trimming Dinámico
```cpp
void AudioManager::outputPcm(const int16_t* pcmData, size_t sampleCount) {
    if (!isReady_.load() || !pcmData || sampleCount == 0) return;
    
    const size_t totalSamples = sampleCount * static_cast<size_t>(channels_);
    
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.insert(queue_.end(), pcmData, pcmData + totalSamples);
    
    // Trimming para evitar overflow (~250ms de buffer)
    if (queue_.size() > kMaxQueuedSamples) {
        size_t cut = queue_.size() - kTrimToSamples;
        cut &= ~static_cast<size_t>(1);  // Mantener alineación stereo
        queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(cut));
    }
}
```

#### clearQueue() - Sincronización con Video
```cpp
void AudioManager::clearQueue() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.clear();
    buffering_ = true;  // Reiniciar pre-buffering
}
```

#### Audio Loop - Grain-based Output
```cpp
void AudioManager::audioLoop() {
    std::vector<int16_t> s16Grain(kGrainFrames * 2, 0);
    
    while (running_.load()) {
        // Obtener grain de samples del queue
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            const size_t needed = kGrainFrames * 2;
            
            if (buffering_) {
                // Pre-roll hasta alcanzar kPreRollSamples (~250ms)
                if (queue_.size() >= kPreRollSamples) {
                    buffering_ = false;
                }
            }
            
            if (!buffering_ && queue_.size() >= needed) {
                for (size_t i = 0; i < needed; ++i) {
                    s16Grain[i] = queue_[i];
                }
                queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(needed));
            } else {
                buffering_ = true;
                std::fill(s16Grain.begin(), s16Grain.end(), 0);
            }
        }
        
        // Output a hardware audio device (SCE Audio API)
        sceAudioOutOutput(audioHandle_, s16Grain.data());
    }
}
```

### Sincronización con Video

#### Flujo de Integración
```cpp
// Cuando se cambia la URL del video:
Mp4PreviewPlayer::setVideoUrl(url) {
    // 1. Clear audio queue inmediatamente
    if (audioManager_) {
        audioManager_->clearQueue();
    }
    
    // 2. Worker thread descarga y decodifica nuevo video
    // 3. AudioManager inicia pre-buffering independiente
    // 4. Video y audio se sincronizan en tiempo real
}
```

#### Latencia y Sincronización
| Componente | Latencia Típica | Notas |
|------------|-----------------|-------|
| AudioManager Pre-roll | ~250ms | Buffer inicial para evitar glitches |
| Mp4PreviewPlayer Decoding | Variable | Depende de CPU, H.264/AAC |
| Total End-to-End | 300-500ms | Aceptable para trailers interactivos |

---

## 6. Ciclo Completo de Navegación y Estado

### Estados de Aplicación (AppState)

```cpp
enum class AppState {
    Initializing,        // Inicialización inicial
    LoginCBL,           // Login Couchbase Lite
    Home,               // Pantalla home con colecciones
    GameDetail,         // Detalle de juego
    MediaViewer,        // Reproductor fullscreen o galería
    StartingSession,    // Preparación de sesión de streaming
    Streaming,          // Gameplay activo
    Profile,            // Perfil del jugador
    Search,             // Búsqueda
    Settings,           // Configuración
};
```

### Transiciones entre Estados

#### Home → Game Detail
```cpp
// En home screen: L1+R1 o Square en tile enfocado
if (pressedNow & luna::input::PAD_SQUARE) {
    appState = AppState::GameDetail;
    currentDetailGame = *focusedGame;
}
```

#### Game Detail → MediaViewer
```cpp
// En detail screen: Square/Right/Left con screenshot o Cross en trailer
if (detailZone == 1 && pressedNow & luna::input::PAD_SQUARE) {
    // Ver screenshot fullscreen
    mediaViewerIdx = focusedGame->screenshots.size() - 1;
    appState = AppState::MediaViewer;
} else if (detailBtnIdx == 1 && pressedNow & luna::input::PAD_CROSS) {
    // Reproducir trailer fullscreen
    player.setVideoUrl(currentDetailGame.videoUrl);
    mediaViewerIdx = -1;
    mediaViewerFromCatalog = true;
    appState = AppState::MediaViewer;
}
```

#### MediaViewer → Game Detail (Circle)
```cpp
// En media viewer: Circle vuelve al detalle del juego
if (pressedNow & luna::input::PAD_CIRCLE) {
    if (mediaViewerFromCatalog) {
        // Volver desde catalog thumbnail
        mediaViewerIdx = -1;
        appState = AppState::GameDetail;
    } else {
        // Volver desde detail screen trailer
        mediaViewerIdx = focusedGame->screenshots.size() - 1;
        appState = AppState::Home;
    }
}
```

#### Game Detail → StartingSession → Streaming
```cpp
// En detail screen: Cross en btnIdx 0 (juego entitulado)
if (detailBtnIdx == 0 && currentDetailGame.isEntitled) {
    activeGameForDetailAndStream = currentDetailGame;
    appState = AppState::StartingSession;
}

// En StartingSession: Esperar Ready de sesión
case AppState::StartingSession: {
    sessionClient.tick();
    
    if (sessionClient.getState() == luna::session::SessionState::Ready) {
        // Configurar RTCPeerConnection con info de sesión
        const auto& info = sessionClient.getSessionInfo();
        rtcClient.setRemoteEndpoint(info.publicIp, info.port);
        rtcClient.applySdpAnswer(info.sdpAnswer);
        rtcClient.setRemoteIceCredentials(info.iceUfrag, info.icePwd);
        
        appState = AppState::Streaming;
    }
}
```

---

## 7. Flujo de Integración: Fullscreen Trailer Player

### Diagrama Detallado

```
┌──────────────────────────────────────────────────────────────────┐
│ USER ACTION                                                      │
│ Square/Right/Left en detail screen con screenshot visible       │
│ o Cross en btnIdx 1 (trailer)                                    │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ main.cpp - Transición a MediaViewer                              │
│ appState = AppState::MediaViewer                                 │
│ mediaViewerIdx = -1 (fullscreen mode)                            │
│ mediaViewerFromCatalog = true                                    │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ DisplayManager::drawMediaViewerScreen()                          │
│ Verifica: currentMediaIdx < 0 o gameScreenshots.empty()          │
│ → Entra en MODO FULLSCREEN                                       │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ Mp4PreviewPlayer::setVideoUrl(game.videoUrl)                     │
│ - Validación URL                                                 │
│ - Reset de estado (isReady, hasDecodedFrame, paused)             │
│ - clearQueue() en AudioManager                                   │
│ - Notifica worker thread via cv_                                 │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
┌─────────────────────────────┐  ┌──────────────────────────────┐
│ Worker Thread               │  │ UI Thread (draw loop)        │
│ - wait() en cv_             │  │ - drawMediaViewerScreen()    │
│ - downloadHttpRange()       │  │   → player.hasFrame()        │
│ - parseMp4()                │  │     false inicialmente       │
│ - decodeVideoPacket()       │  │                               │
│ - decodeAudioPackets()      │  │                               │
└──────────────┬──────────────┘  └──────────────┬───────────────┘
               │                                │
               │ Worker completa primera frame   │ player.hasFrame() == true
               │ decodificada                    │ → drawPreScaledImage(video)
               ▼                                 │
┌──────────────────────────────────────────────────────────────────┐
│ DisplayManager - Renderizado Fullscreen                          │
│ - Video 1920×1080 escalado a player resolution                   │
│ - Controles overlay: barra progreso, tiempo, pause/play          │
│ - Estado badge: "TRÁILER OFICIAL HD (1080p)" o "PAUSADO"        │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ CONTROLS - Interacción del usuario                               │
│                                                                  │
│ Cross (X):                                                       │
│   → Mp4PreviewPlayer::togglePause()                             │
│   → paused_ = !paused_                                          │
│   → hasNewRequest_.store(true)                                  │
│   → WorkerThread notifica y pausa/continua                       │
│                                                                  │
│ L1 + R1:                                                          │
│   → Mp4PreviewPlayer::seekRelative(±150 samples)                │
│   → Calcula nuevo offset basado en sample rate                   │
│   → hasRangeRequest_.store(true)                                │
│   → WorkerThread descarga rango HTTP                             │
│                                                                  │
│ Circle (O):                                                       │
│   → Volver a GameDetail                                          │
│   → player.stop()                                                │
│   → clearMediaQueues()                                           │
│   → appState = AppState::GameDetail                             │
└─────────────────────────────┬────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│ RETURN TO GAME DETAIL                                            │
│                                                                  │
│ Circle en MediaViewer:                                           │
│ - Si mediaViewerFromCatalog: Volver a GameDetail                │
│   (thumbnail thumbnail en hover card)                           │
│ - Si !mediaViewerFromCatalog: Volver a Home                     │
│   (fullscreen trailer desde detail)                              │
└──────────────────────────────────────────────────────────────────┘
```

---

## 8. Controles de Detail Screen con Trailers

### Zona de Screenshot vs Trailer

```cpp
// main.cpp - Game Detail Screen Controls
int detailZone = 0;  // 0: Info text, 1: Screenshot area, 2: Title
int detailBtnIdx = 0;  // 0: Play/Info, 1: Trailer/Screenshot
bool hasScreenshot = !currentDetailGame.screenshots.empty();

// Square/R3 (triángulo) - Navegación screenshot o fullscreen trailer
if (pressedNow & luna::input::PAD_SQUARE) {
    if (detailZone == 0) {
        // Info text → Screenshot area
        detailZone = 1;
        mediaViewerIdx = currentDetailGame.screenshots.size() - 1;
    } else if (detailZone == 1 && hasScreenshot) {
        // Navegar screenshot o fullscreen trailer
        int idx = focusedGame->screenshots.size() - 1;
        if (pressedNow & luna::input::PAD_RIGHT) {
            mediaViewerIdx = (idx + 1) % currentDetailGame.screenshots.size();
        } else if (pressedNow & luna::input::PAD_LEFT) {
            mediaViewerIdx = (idx - 1 + currentDetailGame.screenshots.size()) % 
                           currentDetailGame.screenshots.size();
        } else {
            // Square/R3: Fullscreen trailer
            mediaViewerIdx = -1;
            mediaViewerFromCatalog = false;
            appState = AppState::MediaViewer;
        }
    }
}

// Cross (X) - Acciones según zona
if (pressedNow & luna::input::PAD_CROSS) {
    if (detailZone == 0) {
        // Info text → Play/Info
        if (detailBtnIdx == 0) {
            if (currentDetailGame.isEntitled) {
                startCloudGameSession(currentDetailGame);
            } else {
                g_lastStatusText = "Requiere Luna Premium";
            }
        } else if (detailBtnIdx == 1) {
            // Trailer → MediaViewer fullscreen
            player.setVideoUrl(currentDetailGame.videoUrl);
            mediaViewerIdx = -1;
            appState = AppState::MediaViewer;
        }
    } else if (detailZone == 1 && hasScreenshot) {
        // Screenshot area → Square/R3 fullscreen trailer
        mediaViewerIdx = -1;
        mediaViewerFromCatalog = false;
        appState = AppState::MediaViewer;
    }
}
```

---

## 9. Consideraciones de Diseño

### Ventajas del Patrón Actual

1. **Simplicidad**: Sin overhead de observers o eventos complejos
2. **Rendimiento**: Acceso directo sin copias de estado
3. **Control Centralizado**: main.cpp como single source of truth para navegación
4. **Depuración**: Flujo lineal fácil de seguir en logs

### Desventajas Potenciales

1. **Acoplamiento**: DisplayManager depende directamente de LunaSessionClient
2. **Testabilidad**: Difícil mockear sin modificar main.cpp
3. **Escalabilidad**: Añadir nuevas pantallas requiere más cambios en main.cpp
4. **Maintenability**: Cambios en estructura de estado afectan múltiples puntos

### Alternativas Futuras

1. **Observer Pattern**: LunaSessionClient notifica a DisplayManager
2. **State Management**: Patrón unidireccional (como Redux)
3. **ViewModel**: Separar lógica de UI del acceso a datos
4. **Event Bus**: Desacoplar componentes mediante eventos

---

## 10. Métricas y Performance

### Tiempos Típicos

| Operación | Latencia | Notas |
|-----------|----------|-------|
| FetchCatalog (HTTP) | 2-5 segundos | Dependiente de red |
| parseMp4 (primer frame) | 50-100ms | Depende de CPU |
| AudioManager pre-roll | ~250ms | Buffer inicial |
| Fullscreen trailer start | 300-500ms | End-to-end |
| Screenshot gallery switch | <10ms | Sin descarga |

### Recursos de Memoria

| Componente | Tamaño Estimado | Notas |
|------------|-----------------|-------|
| Mp4PreviewPlayer buffers | ~50 MB | 48 MB download + 2 MB frame + 128 KB AAC |
| AudioManager queue | ~5 MB | Depende de sample rate y duration |
| LunaSessionClient state | <1 MB | Colecciones, perfil, estado |

---

## 11. Casos de Uso Especiales

### Trailers sin Screenshots

```cpp
// Cuando un juego no tiene screenshots pero sí trailer
if (currentMediaIdx < 0 || gameScreenshots.empty()) {
    // MODO FULLSCREEN - Solo video
    drawFullscreenTrailer();
} else {
    // MODO GALERÍA - Screenshots con thumbnail de trailer
    drawScreenshotGallery(gameScreenshots);
}
```

### Hover Card con Video Preview

```cpp
// En home screen: hover sobre tile muestra preview
if (player.hasFrame() && player.currentUrl() == tile.videoUrl) {
    // Video decodificado en tiempo real
    drawPreScaledImage(0, 0, player.getFrameRgba(), ...);
} else {
    // Fallback a imagen estática
    imageLoader.loadAndDrawCover(tile.coverUrl);
}
```

### Sincronización Audio/Video en Streaming

```cpp
// Durante streaming activo:
if (overlayOpen) {
    // Overlay con opciones:
    // 0: Reanudar partida
    // 1: Fijar/Ocultar HUD de rendimiento
    // 2: Salir del juego a Amazon Luna
}

// Teardown de sesión:
void teardownStreamingSession(const std::string& reason, bool autoReturn) {
    rtcClient.stop();
    clearMediaQueues();
    if (autoReturn) {
        appState = AppState::GameDetail;
    }
}
```

---

## 12. Conclusiones

### Integración Exitosa

La integración entre Mp4PreviewPlayer, DisplayManager y LunaSessionClient demuestra:

1. **Arquitectura modular**: Cada componente tiene responsabilidad clara
2. **Sincronización efectiva**: Audio/video coordinados mediante pre-buffering
3. **UX fluida**: Transiciones rápidas entre estados con controles intuitivos
4. **Escalabilidad controlada**: Fácil añadir nuevas funcionalidades

### Lecciones Aprendidas

- El patrón de acceso directo funciona bien para aplicaciones monolíticas
- Worker threads son esenciales para decodificación en tiempo real
- Pre-buffering independiente permite sincronización robusta
- Separar UI (DisplayManager) de datos (LunaSessionClient) mejora mantenibilidad

---

## Referencias

### Archivos Clave

| Archivo | Línea Clave | Descripción |
|---------|-------------|-------------|
| `mp4_preview_player.h` | - | Interfaz del singleton |
| `mp4_preview_player.cpp` | 258-407 | workerLoop, setVideoUrl |
| `display_manager.h` | - | Contratos de DisplayManager |
| `display_manager.cpp` | 1130 | drawHoverPreviewCard |
| `display_manager.cpp` | 1929 | drawMediaViewerScreen |
| `audio_manager.cpp` | 90-165 | outputPcm, audioLoop |
| `main.cpp` | 480-579 | Ciclo principal |
| `main.cpp` | 1390-1404 | Controles home |
| `main.cpp` | 1550, 1591, 1718, 1910 | Acceso a sessionClient |
| `luna_session.h` | - | Interfaz de estado centralizado |

### Métodos Importantes

```cpp
// Mp4PreviewPlayer
setVideoUrl(url)           // Iniciar reproducción
togglePause()              // Pausa/reproducción
seekRelative(samples)      | Búsqueda +/- 5s
hasFrame()                 | Estado frame disponible
getFrameRgba()             | Puntero a buffer video

// AudioManager
outputPcm(pcmData, count)  | Input PCM
clearQueue()               | Reset pre-buffering
isReady()                  | Estado inicializado

// LunaSessionClient
getHomeCollections()       | Colecciones home
getGameNightCollections()  | Colecciones Game Night
getPlayerProfile()         | Perfil del jugador
getState()                 | Estado sesión
getStatusMessage()         | Mensaje de estado
```

---

*Documento generado para Amazon Luna PS5 Simulator - Motor C++ Antigravity*  
*Última actualización: Thu Nov 05 2026*
