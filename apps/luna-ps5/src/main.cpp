#include <cstdio>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <sys/stat.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include "input/pad_manager.h"
#include "stream/webrtc_client.h"
#include "auth/cbl_client.h"
#include "session/luna_session.h"
#include "net/http_client.h"
#include "display/display_manager.h"
#include "audio/audio_manager.h"
#include "media/media_decoder.h"
#include "util/crash_logger.h"
#include "util/image_loader.h"
#include "util/image_loader.h"

namespace {
std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running = false;
}

enum class AppState {
    LoginCBL,
    Catalog,
    GameDetail,
    StartingSession,
    Streaming
};

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // 0. Inicializar CrashLogger sincronizado para diagnostico nativo
    luna::util::CrashLogger::init();
    luna::util::CrashLogger::log("====================================================");
    luna::util::CrashLogger::log("  Amazon Luna PS5 - Native Autonomous Client v1.4.0 ");
    luna::util::CrashLogger::log("====================================================");

    // 1. Inicializar HTTP nativo
    luna::net::HttpClient::init();
    luna::util::ImageLoader::instance().init();

    // 2. Inicializar subsistema de pantalla (1080p60 64KB Macro-Tiled Triple-Buffer)
    luna::display::DisplayManager display;
    if (!display.init(1920, 1080)) {
        std::printf("[Main] Advertencia: pantalla no inicializada en modo completo\n");
    }

    // 3. Inicializar subsistema de audio (48 kHz stereo)
    luna::audio::AudioManager audio;
    audio.init(48000, 2);

    // 4. Inicializar decodificadores de medios (H.264 Hardware Vdec2 + Audio)
    luna::media::VideoDecoder videoDecoder;
    videoDecoder.init(1920, 1088);

    luna::media::AudioDecoder audioDecoder;
    audioDecoder.init(48000, 2);

    // 5. Inicializar mando DualSense (sceUserService + scePad)
    luna::input::PadManager padMgr;
    if (!padMgr.init()) {
        std::printf("[Main] Mando DualSense no detectado inicialmente, reintentando...\n");
    }

    // 6. Cliente WebRTC y enlace de flujo de medios
    luna::stream::WebRtcClient rtcClient;
    luna::stream::StreamConfig cfg;
    cfg.targetWidth = 1920;
    cfg.targetHeight = 1080;
    cfg.targetFps = 60;
    rtcClient.init(cfg);

    std::atomic<bool> videoStreaming{false};
    std::atomic<bool> workersRunning{true};

    struct QueuedVideoPacket {
        std::vector<uint8_t> data;
        uint64_t pts = 0;
    };
    std::mutex videoQueueMutex;
    std::condition_variable videoQueueCv;
    std::deque<QueuedVideoPacket> videoPacketQueue;

    std::mutex audioQueueMutex;
    std::condition_variable audioQueueCv;
    std::deque<std::vector<uint8_t>> audioPacketQueue;

    std::mutex latestFrameMutex;
    std::vector<uint8_t> latestFrameY(1920 * 1080, 0x10);
    std::vector<uint8_t> latestFrameU(960 * 540, 0x80);
    std::vector<uint8_t> latestFrameV(960 * 540, 0x80);
    int latestWidth = 1920;
    int latestHeight = 1080;
    int latestStrideY = 1920;
    int latestStrideU = 960;
    int latestStrideV = 960;
    bool hasValidFrame = false;

    auto clearMediaQueues = [&]() {
        {
            std::lock_guard<std::mutex> lk(videoQueueMutex);
            videoPacketQueue.clear();
        }
        {
            std::lock_guard<std::mutex> lk(audioQueueMutex);
            audioPacketQueue.clear();
        }
        {
            std::lock_guard<std::mutex> lk(latestFrameMutex);
            hasValidFrame = false;
        }
        videoDecoder.resetStreamState();
        videoStreaming.store(false);
    };

    // Network thread callbacks ONLY enqueue packets into bounded queues (never block libdatachannel!)
    rtcClient.onVideo([&](const uint8_t* h264, size_t size, uint64_t pts) {
        if (!h264 || size == 0) return;
        bool needKey = false;
        {
            std::lock_guard<std::mutex> lk(videoQueueMutex);
            if (videoPacketQueue.size() >= 15) {
                videoPacketQueue.pop_front();
                needKey = true;
            }
            videoPacketQueue.push_back(QueuedVideoPacket{std::vector<uint8_t>(h264, h264 + size), pts});
        }
        videoQueueCv.notify_one();
        if (needKey) {
            rtcClient.requestKeyframe();
        }
    });

    rtcClient.onAudio([&](const uint8_t* opus, size_t size, uint64_t pts) {
        (void)pts;
        if (!opus || size == 0) return;
        {
            std::lock_guard<std::mutex> lk(audioQueueMutex);
            if (audioPacketQueue.size() >= 40) {
                audioPacketQueue.pop_front();
            }
            audioPacketQueue.emplace_back(opus, opus + size);
        }
        audioQueueCv.notify_one();
    });

    // Dedicated video decode thread (joined cleanly on exit)
    std::thread videoDecodeThread([&]() {
        while (workersRunning.load()) {
            QueuedVideoPacket pkt;
            {
                std::unique_lock<std::mutex> lk(videoQueueMutex);
                videoQueueCv.wait_for(lk, std::chrono::milliseconds(20), [&]() {
                    return !workersRunning.load() || !videoPacketQueue.empty();
                });
                if (!workersRunning.load()) break;
                if (videoPacketQueue.empty()) continue;
                pkt = std::move(videoPacketQueue.front());
                videoPacketQueue.pop_front();
            }

            luna::media::DecodedVideoFrame frame{};
            if (videoDecoder.decode(pkt.data.data(), pkt.data.size(), pkt.pts, frame)) {
                if (frame.width > 0 && frame.height > 0 && frame.y && frame.u && frame.v &&
                    videoDecoder.getDecodedFrameCount() > 0) {
                    const size_t yBytes = static_cast<size_t>(frame.strideY) * frame.height;
                    const size_t uBytes = static_cast<size_t>(frame.strideU) * (frame.height / 2);
                    const size_t vBytes = static_cast<size_t>(frame.strideV) * (frame.height / 2);
                    {
                        std::lock_guard<std::mutex> lk(latestFrameMutex);
                        if (latestFrameY.size() < yBytes) latestFrameY.resize(yBytes);
                        if (latestFrameU.size() < uBytes) latestFrameU.resize(uBytes);
                        if (latestFrameV.size() < vBytes) latestFrameV.resize(vBytes);
                        std::memcpy(latestFrameY.data(), frame.y, yBytes);
                        std::memcpy(latestFrameU.data(), frame.u, uBytes);
                        std::memcpy(latestFrameV.data(), frame.v, vBytes);
                        latestWidth = frame.width;
                        latestHeight = frame.height;
                        latestStrideY = frame.strideY;
                        latestStrideU = frame.strideU;
                        latestStrideV = frame.strideV;
                        hasValidFrame = true;
                    }
                    videoStreaming.store(true);
                }
            } else if (videoDecoder.needsKeyframe()) {
                rtcClient.requestKeyframe();
            }
        }
    });

    // Dedicated audio decode thread (joined cleanly on exit)
    std::thread audioDecodeThread([&]() {
        std::vector<int16_t> pcmSamples;
        while (workersRunning.load()) {
            std::vector<uint8_t> opusPkt;
            {
                std::unique_lock<std::mutex> lk(audioQueueMutex);
                audioQueueCv.wait_for(lk, std::chrono::milliseconds(20), [&]() {
                    return !workersRunning.load() || !audioPacketQueue.empty();
                });
                if (!workersRunning.load()) break;
                if (audioPacketQueue.empty()) continue;
                opusPkt = std::move(audioPacketQueue.front());
                audioPacketQueue.pop_front();
            }

            if (audioDecoder.decode(opusPkt.data(), opusPkt.size(), pcmSamples)) {
                audio.outputPcm(pcmSamples.data(), pcmSamples.size() / 2);
            }
        }
    });

    // 7. Estado inicial y Cliente de Autenticación Amazon CBL QR autónomo
    AppState appState = AppState::LoginCBL;
    luna::auth::CblClient cbl;
    std::string storageDir = ".";
#if defined(LUNA_TARGET_PS5)
    struct stat st{};
    if (::stat("/data/homebrew/PPSA99825", &st) == 0) {
        storageDir = "/data/homebrew/PPSA99825";
    } else if (::stat("/data/psbox", &st) == 0) {
        storageDir = "/data/psbox";
    }
#endif

    cbl.init(storageDir);
    if (cbl.isAuthenticated()) {
        appState = AppState::Catalog;
    } else {
        appState = AppState::LoginCBL;
    }

    // 9. Cliente de Sesion Amazon Tempo
    luna::session::LunaSessionClient sessionClient;
    std::string g_lastStatusText = "";
    std::string g_catalogDiagnostic = "";
    bool catalogFetched = false;

    // 10. Catalogo base de respaldo y estructura 2D de colecciones oficiales de Amazon Luna
    std::vector<luna::display::GameTile> games;
    {
        luna::display::GameTile g;
        g.id = "amzn1.adg.product.f0b7b7d7-2026-4d5e-8a3f-galacticracer";
        g.title = "STAR WARS: Galactic Racerâ¢";
        g.publisher = "Secret Mode";
        g.developer = "Fuse Games";
        g.releaseYear = "2026";
        g.genres = "Carreras â¢ AcciÃ³n";
        g.inputMethods = "Mando DualSense";
        g.playerCount = "1-4 jugadores";
        g.channelSubtext = "Incluido con Prime";
        g.badgeColor = 0xFF9146FF;
        g.category = "INCLUIDO CON PRIME";
        g.synopsis = "Participa en carreras de alta velocidad por el Borde Exterior de la galaxia de Star Wars.";
        g.heroColor = 0xFF9146FF;
        g.pegiRating = "PEGI 12";
        g.isEntitled = true;
        g.buttonText = "â¶ Jugar ahora";
        games.push_back(std::move(g));
    }

    auto requestTileImages = [&](const luna::display::GameTile& tile) {
        if (!tile.coverUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(tile.coverUrl, luna::util::ImagePreset::VerticalPoster);
        }
        if (!tile.backdropUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(tile.backdropUrl, luna::util::ImagePreset::PanoramicHero);
        }
        if (!tile.logoUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(tile.logoUrl, luna::util::ImagePreset::ProductLogo);
        }
    };

    static const std::vector<std::string> kNavTabs = {
        "Inicio",
        "GameNight",
        "Biblioteca",
        "Luna+",
        "Catálogo A-Z"
    };

    int activeTabIdx = 0;
    int focusZone = 1; // 0 = Top Navbar, 1 = Content (Hero Rotator / Swimlanes)
    int selectedRowIdx = 0;
    std::vector<int> rowSelectedCol;
    int heroBtnIdx = 0; // 0 = Jugar ahora, 1 = Más información

    std::vector<luna::display::LunaCollection> tabCollections[5];

    auto rebuildTabCollections = [&]() {
        // Tab 0: Inicio (landing_home real)
        tabCollections[0] = sessionClient.getHomeCollections();
        if (tabCollections[0].empty()) {
            luna::display::LunaCollection fallbackRow;
            fallbackRow.id = "fallback_row";
            fallbackRow.title = "Destacados en Amazon Luna";
            fallbackRow.items = games;
            tabCollections[0].push_back(fallbackRow);
        }

        // Tab 1: GameNight (multistate_game_night real + colecciones multijugador)
        tabCollections[1] = sessionClient.getGameNightCollections();
        if (tabCollections[1].empty()) {
            for (const auto& col : tabCollections[0]) {
                if (col.id.find("home_2") != std::string::npos || col.title.find("Jugad") != std::string::npos || col.isHeroRotator) {
                    tabCollections[1].push_back(col);
                }
            }
            if (tabCollections[1].empty()) tabCollections[1] = tabCollections[0];
        }

        // Tab 2: Biblioteca (landing_library real + Seguir jugando)
        tabCollections[2] = sessionClient.getLibraryCollections();
        for (const auto& col : tabCollections[0]) {
            if (col.id.find("recently_played") != std::string::npos) {
                bool found = false;
                for (const auto& lc : tabCollections[2]) {
                    if (lc.id == col.id) { found = true; break; }
                }
                if (!found) tabCollections[2].push_back(col);
            }
        }
        if (tabCollections[2].empty()) tabCollections[2] = tabCollections[0];

        // Tab 3: Luna+ (Selecciones del editor, Aventuras, Novedades)
        tabCollections[3].clear();
        for (const auto& col : tabCollections[0]) {
            if (col.isHeroRotator || col.id.find("editors_pick") != std::string::npos ||
                col.id.find("adventure") != std::string::npos || col.id.find("popular") != std::string::npos ||
                col.id.find("recently_added") != std::string::npos) {
                tabCollections[3].push_back(col);
            }
        }
        if (tabCollections[3].empty()) tabCollections[3] = tabCollections[0];

        // Tab 4: Catálogo A-Z (Todos los juegos únicos ordenados alfabéticamente en filas de 10)
        tabCollections[4].clear();
        std::vector<luna::display::GameTile> sortedGames = games;
        std::sort(sortedGames.begin(), sortedGames.end(),
                  [](const luna::display::GameTile& a, const luna::display::GameTile& b) {
                      return a.title < b.title;
                  });
        const size_t kChunk = 10;
        for (size_t i = 0; i < sortedGames.size(); i += kChunk) {
            size_t end = std::min(sortedGames.size(), i + kChunk);
            luna::display::LunaCollection azRow;
            azRow.id = "az_row_" + std::to_string(i / kChunk);
            std::string firstLetter = sortedGames[i].title.empty() ? "A" : sortedGames[i].title.substr(0, 1);
            std::string lastLetter = sortedGames[end - 1].title.empty() ? "Z" : sortedGames[end - 1].title.substr(0, 1);
            azRow.title = "Catálogo Completo A-Z (" + firstLetter + " - " + lastLetter + ") • Juegos " +
                          std::to_string(i + 1) + " a " + std::to_string(end);
            for (size_t k = i; k < end; ++k) {
                azRow.items.push_back(sortedGames[k]);
            }
            tabCollections[4].push_back(std::move(azRow));
        }
        if (tabCollections[4].empty()) tabCollections[4] = tabCollections[0];

        for (auto& g : games) {
            requestTileImages(g);
        }
    };

    auto syncRowSelectionSize = [&]() {
        const auto& cols = tabCollections[activeTabIdx];
        if (rowSelectedCol.size() < cols.size()) {
            rowSelectedCol.resize(cols.size(), 0);
        }
        if (selectedRowIdx >= static_cast<int>(cols.size())) {
            selectedRowIdx = std::max(0, static_cast<int>(cols.size()) - 1);
        }
        if (!cols.empty() && selectedRowIdx >= 0) {
            int maxItems = static_cast<int>(cols[selectedRowIdx].items.size());
            if (rowSelectedCol[selectedRowIdx] >= maxItems) {
                rowSelectedCol[selectedRowIdx] = std::max(0, maxItems - 1);
            }
        }
    };

    luna::display::GameTile activeGameForDetailAndStream = games[0];
    int detailZone = 0;       // 0 = Botones [Jugar ahora / Volver], 1 = Carrusel "También te puede interesar"
    int detailBtnIdx = 0;     // 0 = Jugar ahora, 1 = Volver al catálogo
    int detailSimilarIdx = 0; // Indice en similarGames

    auto loadCatalogIfAuthenticated = [&]() {
        if (cbl.isAuthenticated() && !catalogFetched) {
            luna::util::CrashLogger::log("[Main] Cargando estructura 1:1 de colecciones desde Amazon Luna...");
            cbl.refreshAccessToken();
            auto dyn = sessionClient.fetchCatalog(cbl.getAccessToken(), cbl.getCustomerId(), cbl.getDeviceSerial());
            if (!dyn.empty()) {
                games = std::move(dyn);
                g_lastStatusText = sessionClient.getStatusMessage();
                g_catalogDiagnostic.clear();
                luna::util::CrashLogger::log("[Main] ¡Catalogo dinamico actualizado con %zu juegos!", games.size());
            } else {
                g_catalogDiagnostic = sessionClient.getStatusMessage();
                if (g_catalogDiagnostic.empty()) {
                    g_catalogDiagnostic = "Aviso Catalogo: no se recibieron juegos (usando juegos base)";
                }
                g_lastStatusText = g_catalogDiagnostic;
                luna::util::CrashLogger::log("[Main] fetchCatalog status: %s", g_catalogDiagnostic.c_str());
            }
            rebuildTabCollections();
            selectedRowIdx = 0;
            rowSelectedCol.assign(tabCollections[activeTabIdx].size(), 0);
            catalogFetched = true;
        }
    };

    rebuildTabCollections();
    syncRowSelectionSize();

    if (cbl.isAuthenticated()) {
        loadCatalogIfAuthenticated();
    }

    auto openGameDetail = [&](const luna::display::GameTile& selectedTile) {
        activeGameForDetailAndStream = selectedTile;
        sessionClient.preloadGameDetails(activeGameForDetailAndStream, cbl.getAccessToken(), cbl.getDeviceSerial());

        requestTileImages(activeGameForDetailAndStream);
        if (!activeGameForDetailAndStream.backdropUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(activeGameForDetailAndStream.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
        }
        if (!activeGameForDetailAndStream.logoUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(activeGameForDetailAndStream.logoUrl, luna::util::ImagePreset::ProductLogo);
        }
        for (const auto& shot : activeGameForDetailAndStream.screenshotUrls) {
            luna::util::ImageLoader::instance().requestImage(shot, luna::util::ImagePreset::ScreenshotThumbnail);
        }

        if (activeGameForDetailAndStream.similarGames.empty()) {
            for (const auto& g : games) {
                if (g.id != activeGameForDetailAndStream.id) {
                    activeGameForDetailAndStream.similarGames.push_back(g);
                    if (activeGameForDetailAndStream.similarGames.size() >= 10) break;
                }
            }
        }
        for (auto& sim : activeGameForDetailAndStream.similarGames) {
            requestTileImages(sim);
        }

        for (auto& g : games) {
            if (g.id == activeGameForDetailAndStream.id) g = activeGameForDetailAndStream;
        }
        for (int t = 0; t < 5; ++t) {
            for (auto& col : tabCollections[t]) {
                for (auto& item : col.items) {
                    if (item.id == activeGameForDetailAndStream.id) {
                        item = activeGameForDetailAndStream;
                    }
                }
            }
        }

        detailZone = 0;
        detailBtnIdx = 0;
        detailSimilarIdx = 0;
        appState = AppState::GameDetail;
    };

    auto startCloudGameSession = [&](const luna::display::GameTile& targetTile) {
        if (!targetTile.isEntitled) {
            std::string price = targetTile.secondaryButtonText.empty() ? "9,99 â¬/mes" : targetTile.secondaryButtonText;
            g_lastStatusText = "Este juego requiere suscripcion a Luna Premium (" + price + ").";
            g_catalogDiagnostic = g_lastStatusText;
            luna::util::CrashLogger::log("[Main] Juego no disponible en tu suscripcion (%s)", targetTile.title.c_str());
            return;
        }

        activeGameForDetailAndStream = targetTile;
        std::string targetGameId = activeGameForDetailAndStream.id;
        luna::util::CrashLogger::log("[Main] Iniciando sesion de juego Cloud para: %s (%s)...",
                                     activeGameForDetailAndStream.title.c_str(), targetGameId.c_str());

        cbl.refreshAccessToken();

        std::string sdpOffer = rtcClient.generateSdpOffer();
        bool ok = sessionClient.startSession(targetGameId, cbl.getAccessToken(), sdpOffer,
                                             cbl.getDeviceSerial(), cbl.getCustomerId());
        if (ok) {
            g_lastStatusText = "Solicitando sesion de juego a Amazon Tempo...";
            g_catalogDiagnostic.clear();
            appState = AppState::StartingSession;
        } else {
            g_lastStatusText = sessionClient.getStatusMessage();
            g_catalogDiagnostic = g_lastStatusText;
            luna::util::CrashLogger::logStep(4, ("Fallo: " + g_lastStatusText).c_str());
            luna::util::CrashLogger::log("[Main ERROR] startSession fallo: %s", g_lastStatusText.c_str());
        }
    };

    auto lastNavTime = std::chrono::steady_clock::now();
    uint32_t lastButtons = 0;

    uint32_t seq = 0;
    uint64_t frameCount = 0;
    uint8_t reportBuf[64];
    size_t reportLen = 0;

    // Estado del stream e In-Game Overlay oficial de Luna (Fase 4)
    int streamingActiveFrames = 0;
    bool hudPinned = false;
    bool overlayOpen = false;
    int overlaySelectedIdx = 0;
    bool prevOverlayCombo = false;

    std::printf("[Main] Bucle principal activo a 60 FPS\n");

    while (g_running) {
        padMgr.poll();

        if (padMgr.isExitComboPressed()) {
            break;
        }

        const auto& padState = padMgr.getState(0);
        uint32_t pressedNow = padState.buttons & (~lastButtons);
        lastButtons = padState.buttons;

        auto now = std::chrono::steady_clock::now();
        int navElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastNavTime).count();

        switch (appState) {
            case AppState::LoginCBL: {
                cbl.tick();

                if (cbl.isAuthenticated()) {
                    luna::util::CrashLogger::log("[Main] Usuario autenticado con exito. Cargando catalogo...");
                    loadCatalogIfAuthenticated();
                    appState = AppState::Catalog;
                    break;
                }

                display.drawLoginScreen(
                    cbl.getPublicCode(),
                    cbl.getVerificationUri(),
                    cbl.getQrSize(),
                    [&cbl](int x, int y) { return cbl.getQrModule(x, y); },
                    cbl.getErrorMessage().empty() ? "Esperando a que autorices en tu telefono/PC..." : cbl.getErrorMessage(),
                    frameCount
                );
                break;
            }

            case AppState::Catalog: {
                syncRowSelectionSize();

                // Cambio rapido de pestañas superiores con L2 / R2 desde cualquier zona
                if (pressedNow & luna::input::PAD_L2) {
                    activeTabIdx = (activeTabIdx + static_cast<int>(kNavTabs.size()) - 1) % static_cast<int>(kNavTabs.size());
                    selectedRowIdx = 0;
                    heroBtnIdx = 0;
                    rowSelectedCol.assign(tabCollections[activeTabIdx].size(), 0);
                    lastNavTime = now;
                }
                if (pressedNow & luna::input::PAD_R2) {
                    activeTabIdx = (activeTabIdx + 1) % static_cast<int>(kNavTabs.size());
                    selectedRowIdx = 0;
                    heroBtnIdx = 0;
                    rowSelectedCol.assign(tabCollections[activeTabIdx].size(), 0);
                    lastNavTime = now;
                }

                syncRowSelectionSize();
                auto& activeCols = tabCollections[activeTabIdx];
                bool onHeroRow = (!activeCols.empty() && selectedRowIdx == 0 && activeCols[0].isHeroRotator);

                // L1 / R1: En Hero Rotator cambia de diapositiva; en Swimlanes salta 5 tarjetas
                if (pressedNow & luna::input::PAD_L1) {
                    if (!activeCols.empty() && selectedRowIdx < static_cast<int>(activeCols.size())) {
                        int step = onHeroRow ? 1 : 5;
                        int total = static_cast<int>(activeCols[selectedRowIdx].items.size());
                        if (total > 0) {
                            if (onHeroRow) {
                                rowSelectedCol[0] = (rowSelectedCol[0] + total - 1) % total;
                            } else {
                                rowSelectedCol[selectedRowIdx] = std::max(0, rowSelectedCol[selectedRowIdx] - step);
                            }
                        }
                    }
                    lastNavTime = now;
                }
                if (pressedNow & luna::input::PAD_R1) {
                    if (!activeCols.empty() && selectedRowIdx < static_cast<int>(activeCols.size())) {
                        int step = onHeroRow ? 1 : 5;
                        int total = static_cast<int>(activeCols[selectedRowIdx].items.size());
                        if (total > 0) {
                            if (onHeroRow) {
                                rowSelectedCol[0] = (rowSelectedCol[0] + 1) % total;
                            } else {
                                rowSelectedCol[selectedRowIdx] = std::min(total - 1, rowSelectedCol[selectedRowIdx] + step);
                            }
                        }
                    }
                    lastNavTime = now;
                }

                // Navegacion 2D (ARRIBA/ABAJO entre Zonas y Filas; IZQUIERDA/DERECHA entre Botones/Tarjetas)
                if (navElapsedMs > 155) {
                    bool moved = false;
                    bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -16000);
                    bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);
                    bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000);
                    bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000);

                    if (moveUp) {
                        if (focusZone == 1) {
                            if (selectedRowIdx > 0) {
                                selectedRowIdx--;
                                moved = true;
                            } else {
                                focusZone = 0; // Subir a la Barra de Navegacion Superior
                                moved = true;
                            }
                        }
                    } else if (moveDown) {
                        if (focusZone == 0) {
                            focusZone = 1;
                            selectedRowIdx = 0;
                            moved = true;
                        } else if (selectedRowIdx + 1 < static_cast<int>(activeCols.size())) {
                            selectedRowIdx++;
                            moved = true;
                        }
                    } else if (moveLeft) {
                        if (focusZone == 0) {
                            activeTabIdx = (activeTabIdx + static_cast<int>(kNavTabs.size()) - 1) % static_cast<int>(kNavTabs.size());
                            selectedRowIdx = 0;
                            rowSelectedCol.assign(tabCollections[activeTabIdx].size(), 0);
                            moved = true;
                        } else if (onHeroRow) {
                            if (heroBtnIdx > 0) {
                                heroBtnIdx = 0;
                                moved = true;
                            } else {
                                int total = static_cast<int>(activeCols[0].items.size());
                                if (total > 0) {
                                    rowSelectedCol[0] = (rowSelectedCol[0] + total - 1) % total;
                                    moved = true;
                                }
                            }
                        } else if (!activeCols.empty()) {
                            int& col = rowSelectedCol[selectedRowIdx];
                            if (col > 0) {
                                col--;
                                moved = true;
                            }
                        }
                    } else if (moveRight) {
                        if (focusZone == 0) {
                            activeTabIdx = (activeTabIdx + 1) % static_cast<int>(kNavTabs.size());
                            selectedRowIdx = 0;
                            rowSelectedCol.assign(tabCollections[activeTabIdx].size(), 0);
                            moved = true;
                        } else if (onHeroRow) {
                            if (heroBtnIdx < 1) {
                                heroBtnIdx = 1;
                                moved = true;
                            } else {
                                int total = static_cast<int>(activeCols[0].items.size());
                                if (total > 0) {
                                    rowSelectedCol[0] = (rowSelectedCol[0] + 1) % total;
                                    heroBtnIdx = 0;
                                    moved = true;
                                }
                            }
                        } else if (!activeCols.empty()) {
                            int& col = rowSelectedCol[selectedRowIdx];
                            int total = static_cast<int>(activeCols[selectedRowIdx].items.size());
                            if (col + 1 < total) {
                                col++;
                                moved = true;
                            }
                        }
                    }

                    if (moved) {
                        lastNavTime = now;
                        if (g_lastStatusText.find("Error") == std::string::npos &&
                            g_lastStatusText.find("Fallo") == std::string::npos) {
                            g_lastStatusText.clear();
                        }
                    }
                }

                syncRowSelectionSize();
                auto& curCols = tabCollections[activeTabIdx];

                // Pre-cargar imagenes en background de tarjetas visibles (asincrono y no bloqueante)
                int startR = std::max(0, selectedRowIdx - 1);
                int endR = std::min(static_cast<int>(curCols.size()), selectedRowIdx + 3);
                for (int r = startR; r < endR; ++r) {
                    int selC = (r < static_cast<int>(rowSelectedCol.size())) ? rowSelectedCol[r] : 0;
                    int startC = std::max(0, selC - 2);
                    int endC = std::min(static_cast<int>(curCols[r].items.size()), selC + 6);
                    for (int c = startC; c < endC; ++c) {
                        requestTileImages(curCols[r].items[c]);
                    }
                }

                // Determinar el juego actualmente enfocado
                const luna::display::GameTile* focusedGame = nullptr;
                if (!curCols.empty() && selectedRowIdx >= 0 && selectedRowIdx < static_cast<int>(curCols.size())) {
                    const auto& rowItems = curCols[selectedRowIdx].items;
                    int selC = rowSelectedCol[selectedRowIdx];
                    if (!rowItems.empty() && selC >= 0 && selC < static_cast<int>(rowItems.size())) {
                        focusedGame = &rowItems[selC];
                    }
                }

                // Accion con (X):
                // - En Zona 0 (Navbar): bajar a Zona 1
                // - En Hero Rotator con heroBtnIdx == 0 ("▶ Jugar ahora"): iniciar juego Cloud directo
                // - En Hero Rotator con heroBtnIdx == 1 ("Más información") o en cualquier tarjeta de fila: abrir Ficha GameDetail
                if (pressedNow & luna::input::PAD_CROSS) {
                    if (focusZone == 0) {
                        focusZone = 1;
                        selectedRowIdx = 0;
                    } else if (focusedGame) {
                        bool isHeroPlayNow = (selectedRowIdx == 0 && curCols[0].isHeroRotator && heroBtnIdx == 0);
                        if (isHeroPlayNow) {
                            startCloudGameSession(*focusedGame);
                        } else {
                            openGameDetail(*focusedGame);
                        }
                        break;
                    }
                }

                // Atajo directo con [CUADRADO]: Jugar ahora directamente desde cualquier tarjeta o Hero
                if ((pressedNow & luna::input::PAD_SQUARE) && focusedGame) {
                    startCloudGameSession(*focusedGame);
                    break;
                }

                // Triangulo -> Cerrar sesion de cuenta Amazon
                if (pressedNow & luna::input::PAD_TRIANGLE) {
                    luna::util::CrashLogger::log("[Main] Cerrando sesion de Amazon...");
                    cbl.logout();
                    catalogFetched = false;
                    g_catalogDiagnostic.clear();
                    g_lastStatusText.clear();
                    appState = AppState::LoginCBL;
                    break;
                }

                std::string catalogStatus;
                if (!g_lastStatusText.empty()) {
                    catalogStatus = g_lastStatusText;
                } else if (!g_catalogDiagnostic.empty()) {
                    catalogStatus = g_catalogDiagnostic;
                } else if (focusedGame) {
                    catalogStatus = "Seleccionado: " + focusedGame->title + " (" + focusedGame->publisher +
                                    " • " + focusedGame->pegiRating + ")";
                } else {
                    catalogStatus = "Explora tu catálogo de Amazon Luna";
                }

                display.drawLunaHomeScreen(
                    curCols,
                    kNavTabs,
                    activeTabIdx,
                    focusZone,
                    selectedRowIdx,
                    rowSelectedCol,
                    heroBtnIdx,
                    sessionClient.getPlayerProfile(),
                    padMgr.isConnected(),
                    catalogStatus,
                    frameCount
                );
                break;
            }

            case AppState::GameDetail: {
                for (const auto& sim : activeGameForDetailAndStream.similarGames) {
                    requestTileImages(sim);
                }

                // Circulo (O) -> Volver al Catalogo en la misma fila y posicion
                if (pressedNow & luna::input::PAD_CIRCLE) {
                    luna::util::CrashLogger::log("[Main] Regresando al catalogo desde ficha de detalles");
                    appState = AppState::Catalog;
                    break;
                }

                // Navegacion en Ficha de Detalle (Zona 0: Botones Jugar/Volver, Zona 1: "También te puede interesar")
                if (navElapsedMs > 155) {
                    bool moved = false;
                    bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -16000);
                    bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);
                    bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000);
                    bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000);

                    if (moveDown && detailZone == 0 && !activeGameForDetailAndStream.similarGames.empty()) {
                        detailZone = 1;
                        moved = true;
                    } else if (moveUp && detailZone == 1) {
                        detailZone = 0;
                        moved = true;
                    } else if (moveLeft) {
                        if (detailZone == 0 && detailBtnIdx > 0) {
                            detailBtnIdx = 0;
                            moved = true;
                        } else if (detailZone == 1 && detailSimilarIdx > 0) {
                            detailSimilarIdx--;
                            moved = true;
                        }
                    } else if (moveRight) {
                        if (detailZone == 0 && detailBtnIdx < 1) {
                            detailBtnIdx = 1;
                            moved = true;
                        } else if (detailZone == 1 &&
                                   detailSimilarIdx + 1 < static_cast<int>(activeGameForDetailAndStream.similarGames.size())) {
                            detailSimilarIdx++;
                            moved = true;
                        }
                    }
                    if (moved) lastNavTime = now;
                }

                // Cruz (X) -> Ejecutar accion segun el elemento enfocado en GameDetail
                if (pressedNow & luna::input::PAD_CROSS) {
                    if (detailZone == 0) {
                        if (detailBtnIdx == 0) {
                            if (activeGameForDetailAndStream.isEntitled) {
                                startCloudGameSession(activeGameForDetailAndStream);
                            } else {
                                std::string price = activeGameForDetailAndStream.secondaryButtonText.empty() ? "9,99 â¬/mes" : activeGameForDetailAndStream.secondaryButtonText;
                                g_lastStatusText = "Este juego requiere suscripcion a Luna Premium (" + price + ").";
                            }
                        } else {
                            appState = AppState::Catalog;
                        }
                        break;
                    } else if (detailZone == 1 &&
                               detailSimilarIdx >= 0 &&
                               detailSimilarIdx < static_cast<int>(activeGameForDetailAndStream.similarGames.size())) {
                        luna::display::GameTile chosenSimilar = activeGameForDetailAndStream.similarGames[detailSimilarIdx];
                        openGameDetail(chosenSimilar);
                        break;
                    }
                }

                display.drawGameDetailScreen(
                    activeGameForDetailAndStream,
                    sessionClient.getPlayerProfile(),
                    g_lastStatusText.empty()
                        ? ("Listo para jugar a " + activeGameForDetailAndStream.title + " en 1080p 60 FPS")
                        : g_lastStatusText,
                    false,
                    detailZone,
                    detailBtnIdx,
                    detailSimilarIdx,
                    frameCount
                );
                break;
            }

            case AppState::StartingSession: {
                sessionClient.tick();

                if (sessionClient.getState() == luna::session::SessionState::Ready) {
                    const auto& info = sessionClient.getSessionInfo();
                    luna::util::CrashLogger::log("[Main] Sesion lista: Endpoint %s:%d", info.publicIp.c_str(), info.port);

                    if (!info.publicIp.empty() && info.port > 0) {
                        rtcClient.setRemoteEndpoint(info.publicIp, info.port);
                    }
                    if (!info.sdpAnswer.empty()) {
                        rtcClient.applySdpAnswer(info.sdpAnswer);
                    }
                    if (!info.iceUfrag.empty() && !info.icePwd.empty()) {
                        rtcClient.setRemoteIceCredentials(info.iceUfrag, info.icePwd);
                    }
                    clearMediaQueues();
                    streamingActiveFrames = 0;
                    overlayOpen = false;
                    overlaySelectedIdx = 0;
                    rtcClient.start();
                    luna::util::CrashLogger::logStep(7, "Hardware video & audio decoders ready, streaming active.");
                    g_lastStatusText.clear();
                    appState = AppState::Streaming;
                } else if (sessionClient.getState() == luna::session::SessionState::Failed) {
                    luna::util::CrashLogger::log("[Main CRITICAL] Error al iniciar sesion: %s", sessionClient.getStatusMessage().c_str());
                    g_lastStatusText = sessionClient.getStatusMessage();
                    g_catalogDiagnostic = g_lastStatusText;
                    luna::util::CrashLogger::logStep(4, ("Fallo: " + g_lastStatusText).c_str());
                    if (g_lastStatusText.find("caducada") != std::string::npos ||
                        g_lastStatusText.find("403") != std::string::npos ||
                        g_lastStatusText.find("401") != std::string::npos) {
                        luna::util::CrashLogger::log("[Main] Sesión de cuenta inválida o revocada. Reseteando a código QR...");
                        cbl.logout();
                        catalogFetched = false;
                        appState = AppState::LoginCBL;
                    } else if (g_lastStatusText.find("not fresh") != std::string::npos ||
                               g_lastStatusText.find("re-authenticate") != std::string::npos) {
                        cbl.refreshAccessToken();
                        g_lastStatusText = "Token refrescado. Pulsa (X) para reintentar.";
                        g_catalogDiagnostic = g_lastStatusText;
                        appState = AppState::GameDetail;
                    } else {
                        appState = AppState::GameDetail;
                    }
                }

                // Permitir cancelar con Circulo (O) mientras se solicita el servidor
                if (pressedNow & luna::input::PAD_CIRCLE) {
                    sessionClient.terminateSession(cbl.getAccessToken());
                    g_lastStatusText = "Solicitud de sesion cancelada.";
                    appState = AppState::Catalog;
                    break;
                }

                display.drawGameDetailScreen(
                    activeGameForDetailAndStream,
                    sessionClient.getPlayerProfile(),
                    sessionClient.getStatusMessage(),
                    true,
                    detailZone,
                    detailBtnIdx,
                    detailSimilarIdx,
                    frameCount
                );
                break;
            }

            case AppState::Streaming: {
                const auto& curGame = activeGameForDetailAndStream;
                uint64_t decFrames = videoDecoder.getDecodedFrameCount();
                bool isLiveVideo = videoStreaming.load() && (decFrames > 0);

                // Detectar combo de apertura del Overlay Oficial de Amazon Luna:
                // [L1 + R1 + OPTIONS] o [OPTIONS + TOUCHPAD]
                bool comboL1R1Opt = (padState.buttons & luna::input::PAD_L1) &&
                                    (padState.buttons & luna::input::PAD_R1) &&
                                    (padState.buttons & luna::input::PAD_OPTIONS);
                bool comboOptTouch = (padState.buttons & luna::input::PAD_OPTIONS) &&
                                     (padState.buttons & luna::input::PAD_TOUCH);
                bool comboNow = comboL1R1Opt || comboOptTouch;

                if (!isLiveVideo) {
                    // Antes de que empiece el video: (O) o el combo permiten cancelar y regresar al catalogo
                    if ((pressedNow & luna::input::PAD_CIRCLE) || (comboNow && !prevOverlayCombo)) {
                        luna::util::CrashLogger::log("[Main] Cancelando conexion de stream y regresando al catalogo");
                        rtcClient.stop();
                        sessionClient.terminateSession(cbl.getAccessToken());
                        clearMediaQueues();
                        overlayOpen = false;
                        g_lastStatusText = "Conexion de juego cancelada.";
                        appState = AppState::Catalog;
                        prevOverlayCombo = comboNow;
                        break;
                    }
                } else {
                    // Fase 4: Durante el gameplay activo (videoStreaming == true):
                    // - L3 y Circulo (O) quedan 100% libres para el juego (sprint, agacharse, derrapar, cancelar en juego)
                    // - El combo [L1+R1+OPTIONS] o [OPTIONS+TOUCHPAD] abre/cierra el "Luna In-Game Overlay"
                    if (comboNow && !prevOverlayCombo) {
                        overlayOpen = !overlayOpen;
                        overlaySelectedIdx = 0;
                        lastNavTime = now;
                    }

                    if (overlayOpen) {
                        if (navElapsedMs > 155) {
                            bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -16000);
                            bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);
                            if (moveUp) {
                                overlaySelectedIdx = (overlaySelectedIdx + 2) % 3;
                                lastNavTime = now;
                            } else if (moveDown) {
                                overlaySelectedIdx = (overlaySelectedIdx + 1) % 3;
                                lastNavTime = now;
                            }
                        }

                        if (pressedNow & luna::input::PAD_CIRCLE) {
                            overlayOpen = false;
                        } else if (pressedNow & luna::input::PAD_CROSS) {
                            if (overlaySelectedIdx == 0) {
                                // 0: Reanudar partida
                                overlayOpen = false;
                            } else if (overlaySelectedIdx == 1) {
                                // 1: Fijar / Ocultar HUD de rendimiento
                                hudPinned = !hudPinned;
                                overlayOpen = false;
                            } else if (overlaySelectedIdx == 2) {
                                // 2: Salir del juego a Amazon Luna
                                luna::util::CrashLogger::log("[Main] Saliendo del juego desde Luna In-Game Overlay");
                                rtcClient.stop();
                                sessionClient.terminateSession(cbl.getAccessToken());
                                clearMediaQueues();
                                overlayOpen = false;
                                streamingActiveFrames = 0;
                                g_lastStatusText = "Partida finalizada.";
                                appState = AppState::Catalog;
                                prevOverlayCombo = comboNow;
                                break;
                            }
                        }
                    }
                }
                prevOverlayCombo = comboNow;

                // Enviar entrada de mando al juego Cloud a 60 Hz SOLO cuando el Overlay NO está abierto
                if (!overlayOpen) {
                    padMgr.serializeLunaProtobufReport(reportBuf, sizeof(reportBuf), reportLen, seq++);
                    if (rtcClient.isConnected() && reportLen > 0) {
                        rtcClient.sendInputPacket(reportBuf, reportLen);
                    }
                }

                const auto& info = sessionClient.getSessionInfo();
                const auto& stats = rtcClient.getStats();
                uint32_t vdecRc = videoDecoder.getLastReturnCode();
                uint8_t vdecErr = videoDecoder.getLastDecodeError();

                char vdecHex[64];
                std::snprintf(vdecHex, sizeof(vdecHex), "0x%08X (err:%u)", vdecRc, static_cast<unsigned>(vdecErr));

                if (!isLiveVideo) {
                    std::string statsStr = "RTP Video: " + std::to_string(stats.rtpPacketsReceived) +
                                           " (SPS:" + std::to_string(stats.spsCount) +
                                           " PPS:" + std::to_string(stats.ppsCount) +
                                           " IDR:" + std::to_string(stats.idrCount) +
                                           " P:" + std::to_string(stats.sliceCount) +
                                           ") | Audio: " + std::to_string(stats.audioFramesReceived) +
                                           " | Ensamblados: " + std::to_string(stats.videoFramesAssembled);
                    std::string iceStatsStr = "ICE: " + rtcClient.getIceStateStr() +
                                              " | PC: " + rtcClient.getPeerStateStr() +
                                              " | Decoded: " + std::to_string(decFrames) +
                                              " | Videodec2 RC: " + std::string(vdecHex);
                    display.drawStreamingStatusScreen(
                        curGame,
                        sessionClient.getStatusMessage().empty()
                            ? ("Servidor: " + info.publicIp + ":" + std::to_string(info.port) + " | Esperando primer frame H.264...")
                            : sessionClient.getStatusMessage(),
                        statsStr,
                        iceStatsStr,
                        frameCount
                    );
                } else {
                    streamingActiveFrames++;
                    // Auto-ocultar la barra técnica tras los primeros 3 segundos (180 frames a 60 FPS), salvo que esté fijada
                    bool showHudNow = (streamingActiveFrames <= 180) || hudPinned;

                    std::string hudLine1 = "FPS: 60 | 1080p | RTP Video: " + std::to_string(stats.rtpPacketsReceived) +
                                           " (IDR:" + std::to_string(stats.idrCount) +
                                           ") | Frames: " + std::to_string(decFrames) +
                                           " | Audio Pkts: " + std::to_string(stats.audioFramesReceived);
                    std::string hudLine2 = "[L1 + R1 + OPTIONS] o [OPTIONS + TOUCHPAD]: Abrir menú de partida Amazon Luna";

                    std::lock_guard<std::mutex> lk(latestFrameMutex);
                    if (hasValidFrame) {
                        display.presentYuv420WithHud(
                            latestFrameY.data(), latestFrameU.data(), latestFrameV.data(),
                            latestStrideY, latestStrideU, latestStrideV,
                            latestWidth, latestHeight,
                            showHudNow, hudLine1, hudLine2,
                            overlayOpen, overlaySelectedIdx, hudPinned, curGame.title
                        );
                    }
                }
                break;
            }
        }

        frameCount++;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (appState == AppState::Streaming) {
        sessionClient.terminateSession(cbl.getAccessToken());
    }

    rtcClient.stop();
    workersRunning.store(false);
    videoQueueCv.notify_all();
    audioQueueCv.notify_all();
    if (videoDecodeThread.joinable()) {
        videoDecodeThread.join();
    }
    if (audioDecodeThread.joinable()) {
        audioDecodeThread.join();
    }
    audioDecoder.shutdown();
    videoDecoder.shutdown();
    padMgr.shutdown();
    audio.shutdown();
    display.shutdown();
    luna::util::ImageLoader::instance().shutdown();
    luna::net::HttpClient::shutdown();

    std::printf("[Main] Cierre ordenado completado con exito.\n");
    return 0;
}
