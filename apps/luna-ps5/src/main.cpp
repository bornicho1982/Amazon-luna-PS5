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
#include "auth/auth_server.h"
#include "session/luna_session.h"
#include "net/http_client.h"
#include "display/display_manager.h"
#include "audio/audio_manager.h"
#include "media/media_decoder.h"
#include "util/crash_logger.h"
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

    // --- Favoritos del usuario (persistidos en favorites.json) ---
    std::vector<std::string> favoriteIds;
    std::string favoritesFilePath = storageDir + "/favorites.json";
    auto saveFavorites = [&]() {
        std::string json = "[";
        for (size_t i = 0; i < favoriteIds.size(); ++i) {
            if (i) json += ",";
            json += "\"" + favoriteIds[i] + "\"";
        }
        json += "]";
        FILE* f = std::fopen(favoritesFilePath.c_str(), "wb");
        if (f) {
            std::fwrite(json.data(), 1, json.size(), f);
            std::fclose(f);
        }
        luna::util::CrashLogger::log("[Main] Favoritos guardados (%zu juegos).", favoriteIds.size());
    };
    auto loadFavorites = [&]() {
        FILE* f = std::fopen(favoritesFilePath.c_str(), "rb");
        if (!f) return;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (sz <= 0 || sz > 65536) {
            std::fclose(f);
            return;
        }
        std::string json(static_cast<size_t>(sz), '\0');
        std::fread(&json[0], 1, static_cast<size_t>(sz), f);
        std::fclose(f);
        size_t pos = 0;
        while ((pos = json.find('"', pos)) != std::string::npos) {
            size_t end = json.find('"', pos + 1);
            if (end == std::string::npos) break;
            favoriteIds.push_back(json.substr(pos + 1, end - pos - 1));
            pos = end + 1;
        }
        luna::util::CrashLogger::log("[Main] Favoritos cargados: %zu juegos.", favoriteIds.size());
    };
    loadFavorites();
    auto isFavorite = [&](const std::string& id) {
        for (const auto& f : favoriteIds) if (f == id) return true;
        return false;
    };
    auto toggleFavorite = [&](const std::string& id) {
        for (auto it = favoriteIds.begin(); it != favoriteIds.end(); ++it) {
            if (*it == id) {
                favoriteIds.erase(it);
                saveFavorites();
                return false;
            }
        }
        favoriteIds.push_back(id);
        saveFavorites();
        return true;
    };

    // Obtener IP LAN local para mostrarla en la pantalla de espera
    std::string lanIp = "192.168.18.21";
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        sockaddr_in serv;
        std::memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_addr.s_addr = inet_addr("8.8.8.8");
        serv.sin_port = htons(53);
        if (connect(sock, (const sockaddr*)&serv, sizeof(serv)) == 0) {
            sockaddr_in name;
            socklen_t namelen = sizeof(name);
            if (getsockname(sock, (sockaddr*)&name, &namelen) == 0) {
                char buf[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &name.sin_addr, buf, sizeof(buf))) {
                    lanIp = buf;
                }
            }
        }
#if defined(_WIN32)
        closesocket(sock);
#else
        close(sock);
#endif
    }
    std::string localWebUrl = "http://127.0.0.1:8080/luna";
    std::string lanWebUrl = "http://" + lanIp + ":8080/luna";

    // 8. Servidor HTTP local Web Bridge (http://127.0.0.1:8080/luna)
    luna::auth::AuthServer authServer;
    authServer.start(8080);

    std::mutex webPlayMutex;
    std::string webPlayAsin;
    authServer.onPlayRequested([&](const std::string& asin) {
        luna::util::CrashLogger::log("[Main] Web Bridge: Solicitud de juego para asin=%s", asin.c_str());
        std::lock_guard<std::mutex> lk(webPlayMutex);
        webPlayAsin = asin;
    });

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
        g.title = "STAR WARS: Galactic Racer";
        g.publisher = "Secret Mode";
        g.developer = "Fuse Games";
        g.releaseYear = "2026";
        g.genres = "Carreras, Accion";
        g.inputMethods = "Mando DualSense";
        g.playerCount = "1-4 jugadores";
        g.channelSubtext = "Incluido con Prime";
        g.badgeColor = 0xFF9146FF;
        g.category = "INCLUIDO CON PRIME";
        g.synopsis = "Participa en carreras de alta velocidad por el Borde Exterior de la galaxia de Star Wars.";
        g.heroColor = 0xFF9146FF;
        g.pegiRating = "PEGI 12";
        g.isEntitled = true;
        g.buttonText = "Jugar ahora";
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
        "Mi espacio",
        "Remote Play [NUEVO]"
    };

    int activeTabIdx = 0;
    int navColIdx = 0; // 0..4 = tabs, 5 = Buscar (lupa), 6 = Perfil (B)
    int focusZone = 1; // 0 = Top Navbar, 1 = Content (Hero Rotator / Swimlanes)
    int selectedRowIdx = 0;
    std::vector<int> rowSelectedCol;
    int heroBtnIdx = 0; // 0 = Jugar ahora, 1 = Mas informacion

    bool profileMenuOpen = false;
    int profileMenuIdx = 0;

    bool searchOpen = false;
    int searchZone = 0;
    int searchLetterIdx = 0;
    int searchGridIdx = 0;
    std::vector<luna::display::GameTile> filteredGames;

    static const char* kFilterLetters[] = {
        "TODOS", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
        "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "#"
    };

    auto updateFilteredGames = [&]() {
        filteredGames.clear();
        if (searchLetterIdx == 0) {
            filteredGames = games;
        } else if (searchLetterIdx == 27) { // "#"
            for (const auto& g : games) {
                if (g.title.empty()) continue;
                char c = static_cast<char>(std::toupper(static_cast<unsigned char>(g.title[0])));
                if (c < 'A' || c > 'Z') {
                    filteredGames.push_back(g);
                }
            }
        } else {
            char target = kFilterLetters[searchLetterIdx][0];
            for (const auto& g : games) {
                if (g.title.empty()) continue;
                char c = static_cast<char>(std::toupper(static_cast<unsigned char>(g.title[0])));
                if (c == target) {
                    filteredGames.push_back(g);
                }
            }
        }
        // Filtrar tarjetas no jugables (Luna Premium / promos / canales)
        filteredGames.erase(std::remove_if(filteredGames.begin(), filteredGames.end(),
            [](const luna::display::GameTile& g) {
                return g.title.find("Luna Premium") != std::string::npos ||
                       g.id.find("luna_premium") != std::string::npos ||
                       g.id.find("channel.") != std::string::npos ||
                       g.id.find("promo_") != std::string::npos;
            }),
            filteredGames.end());
        searchGridIdx = 0;
    };

    std::vector<luna::display::LunaCollection> tabCollections[4];

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

        // Tab 1: GameNight (multistate_game_night real + colecciones multijugador y fiesta)
        tabCollections[1] = sessionClient.getGameNightCollections();
        if (tabCollections[1].empty()) {
            luna::display::LunaCollection partyCol;
            partyCol.id = "gamenight_party";
            partyCol.title = "Juegos de fiesta y multijugador";
            for (const auto& g : games) {
                std::string desc = g.synopsis;
                for (auto& ch : desc) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (g.inputMethods.find("jugad") != std::string::npos ||
                    desc.find("multijugador") != std::string::npos ||
                    desc.find("multiplayer") != std::string::npos ||
                    desc.find("party") != std::string::npos ||
                    desc.find("coop") != std::string::npos ||
                    desc.find("cooperativo") != std::string::npos ||
                    g.title.find("Jackbox") != std::string::npos ||
                    g.title.find("Party") != std::string::npos) {
                    partyCol.items.push_back(g);
                }
            }
            if (!partyCol.items.empty()) tabCollections[1].push_back(partyCol);
        }
        if (tabCollections[1].empty()) tabCollections[1] = tabCollections[0];

        // Tab 2: Mi espacio (Biblioteca personal + Mis Favoritos + Seguir jugando)
        tabCollections[2] = sessionClient.getLibraryCollections();
        for (const auto& col : tabCollections[0]) {
            if (col.id.find("recently_played") != std::string::npos ||
                col.id.find("continue") != std::string::npos ||
                col.title.find("Seguir jugando") != std::string::npos) {
                bool found = false;
                for (const auto& lc : tabCollections[2]) {
                    if (lc.id == col.id) { found = true; break; }
                }
                if (!found) tabCollections[2].push_back(col);
            }
        }
        if (!favoriteIds.empty()) {
            luna::display::LunaCollection favCol;
            favCol.id = "user_favorites";
            favCol.title = "Mis juegos favoritos";
            for (const auto& favId : favoriteIds) {
                for (const auto& g : games) {
                    if (g.id == favId) {
                        favCol.items.push_back(g);
                        break;
                    }
                }
            }
            if (!favCol.items.empty()) {
                tabCollections[2].insert(tabCollections[2].begin(), favCol);
            }
        }
        if (tabCollections[2].empty()) {
            luna::display::LunaCollection emptyCol;
            emptyCol.id = "library_empty";
            emptyCol.title = "Tu biblioteca personal";
            emptyCol.items = games;
            tabCollections[2].push_back(emptyCol);
        }

        // Tab 3: Canales / Suscripciones / A-Z
        tabCollections[3].clear();
        for (const auto& col : tabCollections[0]) {
            if (col.id.find("channel") != std::string::npos ||
                col.id.find("subscription") != std::string::npos ||
                col.id.find("prime") != std::string::npos ||
                col.id.find("ubisoft") != std::string::npos ||
                col.id.find("gog") != std::string::npos ||
                col.id.find("luna_plus") != std::string::npos ||
                col.title.find("Prime") != std::string::npos ||
                col.title.find("Ubisoft") != std::string::npos ||
                col.title.find("GOG") != std::string::npos ||
                col.title.find("Luna+") != std::string::npos ||
                col.title.find("Canal") != std::string::npos ||
                col.title.find("Suscrip") != std::string::npos) {
                tabCollections[3].push_back(col);
            }
        }
        if (tabCollections[3].empty()) {
            luna::display::LunaCollection azRow;
            azRow.id = "catalog_az";
            azRow.title = "Todos los juegos (A - Z)";
            azRow.items = games;
            tabCollections[3].push_back(azRow);
        }



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
    int detailZone = 0;           // 0 = Botones [Jugar ahora, Favorito], 1 = Capturas, 2 = "Tambien te puede interesar"
    int detailBtnIdx = 0;         // 0 = Jugar ahora, 1 = Favorito
    int detailScreenshotIdx = 0;
    int detailSimilarIdx = 0;     // Indice en similarGames

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
            authServer.setSessionClient(&sessionClient, cbl.getAccessToken());
            authServer.setCatalog(&games);
        }
    };

    rebuildTabCollections();
    syncRowSelectionSize();

    if (cbl.isAuthenticated()) {
        loadCatalogIfAuthenticated();
    }

    luna::display::GameTile currentDetailGame = games[0];

    auto openGameDetail = [&](const luna::display::GameTile& selectedTile) {
        currentDetailGame = selectedTile;
        sessionClient.preloadGameDetails(currentDetailGame, cbl.getAccessToken(), cbl.getDeviceSerial());

        if (currentDetailGame.backdropUrl.empty()) {
            if (!currentDetailGame.screenshotUrls.empty()) {
                currentDetailGame.backdropUrl = currentDetailGame.screenshotUrls[0];
            } else if (!currentDetailGame.coverUrl.empty()) {
                currentDetailGame.backdropUrl = currentDetailGame.coverUrl;
            }
        }
        requestTileImages(currentDetailGame);
        if (!currentDetailGame.backdropUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(currentDetailGame.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
        }
        if (!currentDetailGame.logoUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(currentDetailGame.logoUrl, luna::util::ImagePreset::ProductLogo);
        }
        for (const auto& shot : currentDetailGame.screenshotUrls) {
            luna::util::ImageLoader::instance().requestImage(shot, luna::util::ImagePreset::ScreenshotThumbnail);
        }

        if (currentDetailGame.similarGames.empty()) {
            for (const auto& g : games) {
                if (g.id != currentDetailGame.id) {
                    currentDetailGame.similarGames.push_back(g);
                    if (currentDetailGame.similarGames.size() >= 10) break;
                }
            }
        }
        for (auto& sim : currentDetailGame.similarGames) {
            requestTileImages(sim);
        }

        activeGameForDetailAndStream = currentDetailGame;
        detailZone = 0;
        detailBtnIdx = 0;
        detailScreenshotIdx = 0;
        detailSimilarIdx = 0;
        appState = AppState::GameDetail;
    };

    // Estado del stream e In-Game Overlay oficial de Luna (Fase 4)
    int streamingActiveFrames = 0;
    bool hudPinned = false;
    bool overlayOpen = false;
    int overlaySelectedIdx = 0;
    bool prevOverlayCombo = false;
    auto teardownStreamingSession = [&](const std::string& reason) {
        luna::util::CrashLogger::log("[Main] teardownStreamingSession: %s", reason.c_str());
        rtcClient.stop();
        sessionClient.terminateSession(cbl.getAccessToken());
        clearMediaQueues();
        overlayOpen = false;
        streamingActiveFrames = 0;
        videoStreaming.store(false);
        g_lastStatusText = reason;
        appState = AppState::Catalog;
    };

    auto startCloudGameSession = [&](const luna::display::GameTile& targetTile) {
        if (!targetTile.isEntitled) {
            std::string price = targetTile.secondaryButtonText.empty() ? "9,99 EUR/mes" : targetTile.secondaryButtonText;
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


    std::printf("[Main] Bucle principal activo a 60 FPS\n");

    while (g_running) {
        padMgr.poll();

        // Procesar peticion de lanzamiento de juego recibida desde el Web Bridge (/api/play?asin=...)
        std::string reqPlayAsin;
        {
            std::lock_guard<std::mutex> lk(webPlayMutex);
            if (!webPlayAsin.empty()) {
                reqPlayAsin = webPlayAsin;
                webPlayAsin.clear();
            }
        }
        if (!reqPlayAsin.empty()) {
            luna::util::CrashLogger::log("[Main] Lanzando juego desde Web Bridge: %s", reqPlayAsin.c_str());
            const luna::display::GameTile* foundTile = nullptr;
            for (const auto& g : games) {
                if (g.id == reqPlayAsin) {
                    foundTile = &g;
                    break;
                }
            }
            if (foundTile) {
                if (appState == AppState::Streaming || appState == AppState::StartingSession) {
                    teardownStreamingSession("Cambiando a juego seleccionado en Web Bridge");
                }
                startCloudGameSession(*foundTile);
            } else {
                luna::display::GameTile webTile;
                webTile.id = reqPlayAsin;
                webTile.title = "Juego Web Amazon Luna";
                webTile.isEntitled = true;
                if (appState == AppState::Streaming || appState == AppState::StartingSession) {
                    teardownStreamingSession("Cambiando a juego seleccionado en Web Bridge");
                }
                startCloudGameSession(webTile);
            }
        }

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
                    luna::util::CrashLogger::log("[Main] Usuario autenticado con exito. Abriendo Web...");
                    display.drawLoginScreen(
                        cbl.getPublicCode(),
                        cbl.getVerificationUri(),
                        cbl.getQrSize(),
                        [&cbl](int x, int y) { return cbl.getQrModule(x, y); },
                        "Conectado a Amazon. Abriendo interfaz Web de Luna...",
                        frameCount
                    );
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
                if (profileMenuOpen) {
                    if (navElapsedMs > 155) {
                        bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -16000);
                        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);
                        if (moveUp) {
                            profileMenuIdx = (profileMenuIdx + 2) % 3;
                            lastNavTime = now;
                        } else if (moveDown) {
                            profileMenuIdx = (profileMenuIdx + 1) % 3;
                            lastNavTime = now;
                        }
                    }

                    if (pressedNow & luna::input::PAD_CIRCLE) {
                        profileMenuOpen = false;
                    } else if (pressedNow & luna::input::PAD_CROSS) {
                        if (profileMenuIdx == 0) {
                            luna::util::CrashLogger::log("[Main] Recargando catalogo desde menu de perfil...");
                            catalogFetched = false;
                            loadCatalogIfAuthenticated();
                            profileMenuOpen = false;
                        } else if (profileMenuIdx == 1) {
                            luna::util::CrashLogger::log("[Main] Cerrando sesion desde menu de perfil...");
                            cbl.logout();
                            catalogFetched = false;
                            g_catalogDiagnostic.clear();
                            g_lastStatusText.clear();
                            appState = AppState::LoginCBL;
                            profileMenuOpen = false;
                            break;
                        } else if (profileMenuIdx == 2) {
                            profileMenuOpen = false;
                        }
                    }
                } else if (searchOpen) {
                    if (pressedNow & luna::input::PAD_CIRCLE) {
                        if (searchZone == 1) {
                            searchZone = 0;
                        } else {
                            searchOpen = false;
                        }
                    }

                    if (searchZone == 0) {
                        if (pressedNow & luna::input::PAD_L1) {
                            searchLetterIdx = (searchLetterIdx + 28 - 5) % 28;
                            updateFilteredGames();
                            lastNavTime = now;
                        } else if (pressedNow & luna::input::PAD_R1) {
                            searchLetterIdx = (searchLetterIdx + 5) % 28;
                            updateFilteredGames();
                            lastNavTime = now;
                        }

                        if (navElapsedMs > 155) {
                            bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000);
                            bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000);
                            bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);

                            if (moveLeft) {
                                searchLetterIdx = (searchLetterIdx + 28 - 1) % 28;
                                updateFilteredGames();
                                lastNavTime = now;
                            } else if (moveRight) {
                                searchLetterIdx = (searchLetterIdx + 1) % 28;
                                updateFilteredGames();
                                lastNavTime = now;
                            } else if (moveDown && !filteredGames.empty()) {
                                searchZone = 1;
                                searchGridIdx = 0;
                                lastNavTime = now;
                            }
                        }

                        if ((pressedNow & luna::input::PAD_CROSS) && !filteredGames.empty()) {
                            searchZone = 1;
                            searchGridIdx = 0;
                        }
                    } else if (searchZone == 1) {
                        int numF = static_cast<int>(filteredGames.size());
                        if (navElapsedMs > 155) {
                            bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000);
                            bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000);
                            bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -16000);
                            bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);

                            if (moveLeft && searchGridIdx > 0) {
                                searchGridIdx--;
                                lastNavTime = now;
                            } else if (moveRight && searchGridIdx + 1 < numF) {
                                searchGridIdx++;
                                lastNavTime = now;
                            } else if (moveUp) {
                                if (searchGridIdx >= 6) {
                                    searchGridIdx -= 6;
                                } else {
                                    searchZone = 0;
                                }
                                lastNavTime = now;
                            } else if (moveDown && searchGridIdx + 6 < numF) {
                                searchGridIdx += 6;
                                lastNavTime = now;
                            }
                        }

                        if (pressedNow & luna::input::PAD_CROSS) {
                            if (searchGridIdx >= 0 && searchGridIdx < numF) {
                                openGameDetail(filteredGames[searchGridIdx]);
                                searchOpen = false;
                                break;
                            }
                        }
                    }
                } else {
                    // L1 / R1 para cambiar de pestaña en cualquier momento
                    if (pressedNow & luna::input::PAD_L1) {
                        activeTabIdx = (activeTabIdx + static_cast<int>(kNavTabs.size()) - 1) % static_cast<int>(kNavTabs.size());
                        syncRowSelectionSize();
                        selectedRowIdx = 0;
                        focusZone = 1;
                        lastNavTime = now;
                    } else if (pressedNow & luna::input::PAD_R1) {
                        activeTabIdx = (activeTabIdx + 1) % static_cast<int>(kNavTabs.size());
                        syncRowSelectionSize();
                        selectedRowIdx = 0;
                        focusZone = 1;
                        lastNavTime = now;
                    }

                    const auto& curCols = tabCollections[activeTabIdx];
                    int numRows = static_cast<int>(curCols.size());

                    if (navElapsedMs > 155) {
                        bool moved = false;
                        bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000);
                        bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000);
                        bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -16000);
                        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);

                        if (focusZone == 0) {
                            // En Navbar Superior
                            int maxNavCols = static_cast<int>(kNavTabs.size()) + 2;
                            if (moveLeft && navColIdx > 0) {
                                navColIdx--;
                                moved = true;
                            } else if (moveRight && navColIdx + 1 < maxNavCols) {
                                navColIdx++;
                                moved = true;
                            } else if (moveDown) {
                                focusZone = 1;
                                selectedRowIdx = 0;
                                moved = true;
                            }
                        } else {
                            // En Contenido (Hero Rotator o Swimlanes)
                            if (moveUp) {
                                if (selectedRowIdx == 0) {
                                    focusZone = 0;
                                    navColIdx = activeTabIdx;
                                    moved = true;
                                } else if (selectedRowIdx > 0) {
                                    selectedRowIdx--;
                                    moved = true;
                                }
                            } else if (moveDown && selectedRowIdx + 1 < numRows) {
                                selectedRowIdx++;
                                moved = true;
                            } else if (moveLeft) {
                                if (selectedRowIdx < static_cast<int>(rowSelectedCol.size())) {
                                    int maxC = (selectedRowIdx < numRows) ? static_cast<int>(curCols[selectedRowIdx].items.size()) : 1;
                                    if (selectedRowIdx == 0 && curCols[0].isHeroRotator) {
                                        if (heroBtnIdx == 1) {
                                            heroBtnIdx = 0;
                                        } else {
                                            rowSelectedCol[0] = (rowSelectedCol[0] + maxC - 1) % std::max(1, maxC);
                                            heroBtnIdx = 0;
                                        }
                                    } else if (rowSelectedCol[selectedRowIdx] > 0) {
                                        rowSelectedCol[selectedRowIdx]--;
                                    }
                                    moved = true;
                                }
                            } else if (moveRight) {
                                if (selectedRowIdx < static_cast<int>(rowSelectedCol.size())) {
                                    int maxC = (selectedRowIdx < numRows) ? static_cast<int>(curCols[selectedRowIdx].items.size()) : 1;
                                    if (selectedRowIdx == 0 && curCols[0].isHeroRotator) {
                                        bool isLunaPrem = (rowSelectedCol[0] == 0);
                                        if (!curCols.empty() && !curCols[0].items.empty() && rowSelectedCol[0] < static_cast<int>(curCols[0].items.size())) {
                                            if (curCols[0].items[rowSelectedCol[0]].title == "Luna Premium") isLunaPrem = true;
                                        }
                                        if (heroBtnIdx == 0 && !isLunaPrem) {
                                            heroBtnIdx = 1;
                                        } else {
                                            rowSelectedCol[0] = (rowSelectedCol[0] + 1) % std::max(1, maxC);
                                            heroBtnIdx = 0;
                                        }
                                    } else if (rowSelectedCol[selectedRowIdx] + 1 < maxC) {
                                        rowSelectedCol[selectedRowIdx]++;
                                    }
                                    moved = true;
                                }
                            }
                        }
                        if (moved) lastNavTime = now;
                    }

                    // Determinar el juego actualmente enfocado
                    const luna::display::GameTile* focusedGame = nullptr;
                    if (!curCols.empty() && selectedRowIdx >= 0 && selectedRowIdx < numRows) {
                        const auto& rowItems = curCols[selectedRowIdx].items;
                        int selC = (selectedRowIdx < static_cast<int>(rowSelectedCol.size())) ? rowSelectedCol[selectedRowIdx] : 0;
                        if (selC >= 0 && selC < static_cast<int>(rowItems.size())) {
                            focusedGame = &rowItems[selC];
                        }
                    }

                    // Accion con (X):
                    if (pressedNow & luna::input::PAD_CROSS) {
                        if (focusZone == 0) {
                            if (navColIdx < static_cast<int>(kNavTabs.size())) {
                                activeTabIdx = navColIdx;
                                syncRowSelectionSize();
                                focusZone = 1;
                                selectedRowIdx = 0;
                            } else if (navColIdx == static_cast<int>(kNavTabs.size())) {
                                searchOpen = true;
                                searchZone = 0;
                                searchLetterIdx = 0;
                                updateFilteredGames();
                            } else if (navColIdx == static_cast<int>(kNavTabs.size()) + 1) {
                                profileMenuOpen = true;
                                profileMenuIdx = 0;
                            }
                        } else if (focusedGame) {
                            bool isHeroSlide = (selectedRowIdx == 0 && curCols[0].isHeroRotator);
                            if (isHeroSlide) {
                                if (focusedGame->title == "Luna Premium") {
                                    // Mas informacion en Luna Premium: hacer scroll al catalogo
                                    if (numRows > 1) {
                                        selectedRowIdx = 1;
                                    }
                                } else {
                                    if (heroBtnIdx == 0) {
                                        startCloudGameSession(*focusedGame);
                                    } else {
                                        openGameDetail(*focusedGame);
                                    }
                                }
                            } else {
                                openGameDetail(*focusedGame);
                            }
                            break;
                        }
                    }

                    // Atajo directo [CUADRADO]: Jugar inmediatamente
                    if ((pressedNow & luna::input::PAD_SQUARE) && focusedGame && focusedGame->title != "Luna Premium") {
                        startCloudGameSession(*focusedGame);
                        break;
                    }

                    // Boton TRIANGULO: Cerrar sesion de cuenta Amazon
                    if (pressedNow & luna::input::PAD_TRIANGLE) {
                        luna::util::CrashLogger::log("[Main] [TRIANGULO] Cerrando sesion de Amazon...");
                        cbl.logout();
                        catalogFetched = false;
                        g_catalogDiagnostic.clear();
                        g_lastStatusText.clear();
                        appState = AppState::LoginCBL;
                        break;
                    }

                    // Boton CIRCULO: Volver a la primera fila
                    if (pressedNow & luna::input::PAD_CIRCLE) {
                        focusZone = 1;
                        selectedRowIdx = 0;
                    }
                }

                const auto& curCols = tabCollections[activeTabIdx];
                std::string catalogStatus;
                if (!g_lastStatusText.empty()) {
                    catalogStatus = g_lastStatusText;
                } else if (!g_catalogDiagnostic.empty()) {
                    catalogStatus = g_catalogDiagnostic;
                } else {
                    catalogStatus = "Explora tu catalogo de Amazon Luna | (X) Ver detalles | ([ ]) Jugar directo";
                }

                display.drawLunaHomeScreen(
                    curCols,
                    kNavTabs,
                    activeTabIdx,
                    focusZone,
                    navColIdx,
                    selectedRowIdx,
                    rowSelectedCol,
                    heroBtnIdx,
                    sessionClient.getPlayerProfile(),
                    padMgr.isConnected(),
                    catalogStatus,
                    frameCount,
                    profileMenuOpen,
                    profileMenuIdx,
                    searchOpen,
                    searchLetterIdx,
                    searchGridIdx,
                    filteredGames
                );
                break;
            }

            case AppState::GameDetail: {
                for (const auto& sim : currentDetailGame.similarGames) {
                    requestTileImages(sim);
                }

                size_t numScreens = currentDetailGame.screenshotUrls.size();
                size_t numSimilar = currentDetailGame.similarGames.size();
                int simZone = (numScreens > 0) ? 2 : 1;

                // Circulo (O) -> Volver al Catalogo
                if (pressedNow & luna::input::PAD_CIRCLE) {
                    luna::util::CrashLogger::log("[Main] Regresando al catalogo desde ficha de detalles");
                    appState = AppState::Catalog;
                    break;
                }

                // Navegacion en Ficha de Detalle (Zona 0: Botones, Zona 1: Capturas, Zona 2 / 1: Similares)
                if (navElapsedMs > 155) {
                    bool moved = false;
                    bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -16000);
                    bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);
                    bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000);
                    bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000);

                    if (moveDown) {
                        if (detailZone == 0) {
                            if (numScreens > 0) {
                                detailZone = 1;
                                detailScreenshotIdx = 0;
                                moved = true;
                            } else if (numSimilar > 0) {
                                detailZone = simZone;
                                detailSimilarIdx = 0;
                                moved = true;
                            }
                        } else if (detailZone == 1 && numScreens > 0 && numSimilar > 0) {
                            detailZone = 2;
                            detailSimilarIdx = 0;
                            moved = true;
                        }
                    } else if (moveUp) {
                        if (detailZone == 2) {
                            detailZone = (numScreens > 0) ? 1 : 0;
                            moved = true;
                        } else if (detailZone == 1) {
                            detailZone = 0;
                            moved = true;
                        }
                    } else if (moveLeft) {
                        if (detailZone == 0 && detailBtnIdx > 0) {
                            detailBtnIdx = 0;
                            moved = true;
                        } else if (detailZone == 1 && numScreens > 0 && detailScreenshotIdx > 0) {
                            detailScreenshotIdx--;
                            moved = true;
                        } else if (detailZone == simZone && detailSimilarIdx > 0) {
                            detailSimilarIdx--;
                            moved = true;
                        }
                    } else if (moveRight) {
                        if (detailZone == 0 && detailBtnIdx < 1) {
                            detailBtnIdx = 1;
                            moved = true;
                        } else if (detailZone == 1 && numScreens > 0 && detailScreenshotIdx + 1 < static_cast<int>(numScreens) && detailScreenshotIdx + 1 < 4) {
                            detailScreenshotIdx++;
                            moved = true;
                        } else if (detailZone == simZone && detailSimilarIdx + 1 < static_cast<int>(numSimilar) && detailSimilarIdx + 1 < 8) {
                            detailSimilarIdx++;
                            moved = true;
                        }
                    }
                    if (moved) lastNavTime = now;
                }

                // Triangulo (△) -> Alternar Favorito
                if (pressedNow & luna::input::PAD_TRIANGLE) {
                    toggleFavorite(currentDetailGame.id);
                    saveFavorites();
                    rebuildTabCollections();
                    g_lastStatusText = isFavorite(currentDetailGame.id) ? "Juego anadido a Favoritos." : "Juego quitado de Favoritos.";
                }

                // Cruz (X) -> Ejecutar accion segun el elemento enfocado en GameDetail
                if (pressedNow & luna::input::PAD_CROSS) {
                    if (detailZone == 0) {
                        if (detailBtnIdx == 0) {
                            if (currentDetailGame.isEntitled) {
                                activeGameForDetailAndStream = currentDetailGame;
                                startCloudGameSession(currentDetailGame);
                            } else {
                                std::string price = currentDetailGame.secondaryButtonText.empty() ? "9,99 EUR/mes" : currentDetailGame.secondaryButtonText;
                                g_lastStatusText = "Este juego requiere suscripcion a Luna Premium (" + price + ").";
                            }
                        } else {
                            toggleFavorite(currentDetailGame.id);
                            saveFavorites();
                            rebuildTabCollections();
                            g_lastStatusText = isFavorite(currentDetailGame.id) ? "Juego anadido a Favoritos." : "Juego quitado de Favoritos.";
                        }
                        break;
                    } else if (detailZone == simZone && detailSimilarIdx >= 0 && detailSimilarIdx < static_cast<int>(numSimilar)) {
                        openGameDetail(currentDetailGame.similarGames[detailSimilarIdx]);
                        break;
                    }
                }

                int selectedCarouselIdx = (detailZone == 1 && numScreens > 0) ? detailScreenshotIdx : detailSimilarIdx;

                display.drawGameDetailScreen(
                    currentDetailGame,
                    sessionClient.getPlayerProfile(),
                    g_lastStatusText.empty()
                        ? ("Listo para jugar a " + activeGameForDetailAndStream.title + " en 1080p 60 FPS")
                        : g_lastStatusText,
                    false,
                    detailZone,
                    detailBtnIdx,
                    selectedCarouselIdx,
                    frameCount,
                    isFavorite(currentDetailGame.id)
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
                    std::string failMsg = sessionClient.getStatusMessage();
                    if (failMsg.find("caducada") != std::string::npos ||
                        failMsg.find("403") != std::string::npos ||
                        failMsg.find("401") != std::string::npos) {
                        luna::util::CrashLogger::log("[Main] Sesion de cuenta invalida o revocada. Reseteando a codigo QR...");
                        cbl.logout();
                        catalogFetched = false;
                        teardownStreamingSession("Sesion expirada");
                        appState = AppState::LoginCBL;
                    } else if (failMsg.find("not fresh") != std::string::npos ||
                               failMsg.find("re-authenticate") != std::string::npos) {
                        cbl.refreshAccessToken();
                        teardownStreamingSession("Token refrescado. Pulsa (X) para reintentar.");
                        appState = AppState::GameDetail;
                    } else {
                        teardownStreamingSession("Error: " + failMsg);
                        appState = AppState::GameDetail;
                    }
                }

                // Permitir cancelar con Circulo (O) mientras se solicita el servidor
                if (pressedNow & luna::input::PAD_CIRCLE) {
                    teardownStreamingSession("Solicitud de sesion cancelada.");
                    break;
                }

                display.drawGameDetailScreen(
                    activeGameForDetailAndStream,
                    sessionClient.getPlayerProfile(),
                    sessionClient.getStatusMessage(),
                    true,
                    detailZone,
                    detailBtnIdx,
                    (detailZone == 1 && !activeGameForDetailAndStream.screenshotUrls.empty()) ? detailScreenshotIdx : detailSimilarIdx,
                    frameCount,
                    isFavorite(activeGameForDetailAndStream.id)
                );
                break;
            }

            case AppState::Streaming: {
                const auto& curGame = activeGameForDetailAndStream;
                uint64_t decFrames = videoDecoder.getDecodedFrameCount();
                bool isLiveVideo = videoStreaming.load() && (decFrames > 0);

                // Deteccion automatica de fin de sesion remota: si el usuario sale
                // del juego desde el menu interno del propio juego, Amazon corta el
                // stream (RTP + DataChannels) o se desconecta ICE/PeerConnection.
                // Regresar inmediatamente al catalogo sin exigir L1+R1+OPTIONS.
                if (rtcClient.hasRemoteSessionEnded()) {
                    teardownStreamingSession("El juego ha finalizado en Amazon Luna.");
                    break;
                }

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
                        teardownStreamingSession("Conexion de juego cancelada.");
                        prevOverlayCombo = comboNow;
                        break;
                    }
                } else {
                    // Durante el gameplay activo (videoStreaming == true):
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
                                teardownStreamingSession("Partida finalizada.");
                                prevOverlayCombo = comboNow;
                                break;
                            }
                        }
                    }
                }
                prevOverlayCombo = comboNow;

                // Enviar entrada de mando al juego Cloud a 60 Hz SOLO cuando el Overlay NO esta abierto
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
                    // Auto-ocultar la barra tecnica tras los primeros 3 segundos (180 frames a 60 FPS), salvo que este fijada
                    bool showHudNow = (streamingActiveFrames <= 180) || hudPinned;

                    std::string hudLine1 = "FPS: 60 | 1080p | RTP Video: " + std::to_string(stats.rtpPacketsReceived) +
                                           " (IDR:" + std::to_string(stats.idrCount) +
                                           ") | Frames: " + std::to_string(decFrames) +
                                           " | Audio Pkts: " + std::to_string(stats.audioFramesReceived);
                    std::string hudLine2 = "[L1 + R1 + OPTIONS] o [OPTIONS + TOUCHPAD]: Abrir menu de partida Amazon Luna";

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

    if (appState == AppState::Streaming || appState == AppState::StartingSession) {
        teardownStreamingSession("Cierre de aplicacion");
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
    authServer.stop();
    display.shutdown();
    luna::util::ImageLoader::instance().shutdown();
    luna::net::HttpClient::shutdown();

    std::printf("[Main] Cierre ordenado completado con exito.\n");
    return 0;
}
