#include <cstdio>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <csignal>
#include <cstring>
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
#include "webrtc_client_2c43058.h"
#include "auth/cbl_client.h"
#include "session/luna_session.h"
#include "net/http_client.h"
#include "display_manager_2c43058.h"
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

    rtcClient.onVideo([&videoDecoder, &display, &rtcClient, &videoStreaming](const uint8_t* h264, size_t size, uint64_t pts) {
        luna::media::DecodedVideoFrame frame;
        if (videoDecoder.decode(h264, size, pts, frame)) {
            videoStreaming = true;
            display.presentYuv420(frame.y, frame.u, frame.v,
                                  frame.strideY, frame.strideU, frame.strideV,
                                  frame.width, frame.height);
        } else if (videoDecoder.needsKeyframe()) {
            rtcClient.requestKeyframe();
        }
    });

    rtcClient.onAudio([&audioDecoder, &audio](const uint8_t* opus, size_t size, uint64_t pts) {
        (void)pts;
        std::vector<int16_t> pcmSamples;
        if (audioDecoder.decode(opus, size, pcmSamples)) {
            audio.outputPcm(pcmSamples.data(), pcmSamples.size() / 2);
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

    // 10. Catalogo oficial de juegos Amazon Luna (con portadas y detalles oficiales)
    std::vector<luna::display::GameTile> games = {
        {
            "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16",
            "Fortnite Battle Royale",
            "Epic Games",
            0xFF0284C7, // Sky Blue
            "GRATIS CON PRIME",
            "Compite para ser el ultimo superviviente en el battle royale mas popular.",
            0xFF0284C7,
            "", {}, 0, 0
        },
        {
            "amzn1.luna.product.47a3c580-8ff5-42d9-85cd-ade28138cf51",
            "Star Wars Outlaws",
            "Ubisoft",
            0xFFF59E0B, // Amber
            "UBISOFT CONNECT",
            "Disfruta del primer mundo abierto de Star Wars explorando el bajo mundo galactico.",
            0xFFF59E0B,
            "", {}, 0, 0
        },
        {
            "amzn1.luna.product.91338a7f-29a1-462e-95f0-72b46ceff340",
            "Avatar: Frontiers of Pandora",
            "Ubisoft",
            0xFF10B981, // Emerald Green
            "UBISOFT CONNECT",
            "Reconecta con tus origenes na'vi y defiende la frontera occidental de Pandora.",
            0xFF10B981,
            "", {}, 0, 0
        },
        {
            "amzn1.luna.product.trackmania",
            "Trackmania",
            "Ubisoft",
            0xFF06B6D4, // Cyan
            "LUNA+",
            "Conduce a velocidades de vertigo en pistas desafiantes creadas por la comunidad.",
            0xFF06B6D4,
            "", {}, 0, 0
        },
        {
            "amzn1.luna.product.588cc88e-de1f-427b-97ba-7b2e42846894",
            "Sonic x Shadow Generations",
            "SEGA",
            0xFFEF4444, // Red
            "LUNA+",
            "Juega como Shadow en una campana con nuevos poderes y recorre etapas iconicas.",
            0xFFEF4444,
            "", {}, 0, 0
        },
        {
            "amzn1.luna.product.falloutnv",
            "Fallout: New Vegas",
            "Bethesda Softworks",
            0xFF8B5CF6, // Purple
            "PRIME GAMING",
            "Lucha por el control del Mojave y la presa Hoover en este aclamado juego de rol.",
            0xFF8B5CF6,
            "", {}, 0, 0
        }
    };

    static const std::vector<std::string> kCategories = {
        "TODOS LOS JUEGOS",
        "DESTACADOS",
        "LUNA+",
        "PRIME GAMING",
        "MI BIBLIOTECA"
    };
    int selectedCategoryIdx = 0;

    auto getFilteredIndices = [&]() -> std::vector<int> {
        std::vector<int> indices;
        const std::string& cat = kCategories[selectedCategoryIdx];
        for (int i = 0; i < static_cast<int>(games.size()); ++i) {
            if (cat == "TODOS LOS JUEGOS") {
                indices.push_back(i);
            } else if (cat == "DESTACADOS") {
                if (games[i].category == "DESTACADOS" || games[i].category == "TODOS") indices.push_back(i);
            } else if (cat == "LUNA+") {
                if (games[i].category == "LUNA+" || games[i].category == "SELECCION" || games[i].category == "AVENTURA") indices.push_back(i);
            } else if (cat == "PRIME GAMING") {
                if (games[i].category == "PRIME GAMING" || games[i].category == "POPULARES") indices.push_back(i);
            } else if (cat == "MI BIBLIOTECA") {
                if (games[i].category == "MI BIBLIOTECA") indices.push_back(i);
            }
        }
        if (indices.empty()) {
            for (int i = 0; i < static_cast<int>(games.size()); ++i) indices.push_back(i);
        }
        return indices;
    };

    int selectedGameIndex = 0;

    auto loadCatalogIfAuthenticated = [&]() {
        if (cbl.isAuthenticated() && !catalogFetched) {
            luna::util::CrashLogger::log("[Main] Cargando catalogo dinamico desde Amazon Luna...");
            cbl.refreshAccessToken();
            auto dyn = sessionClient.fetchCatalog(cbl.getAccessToken(), cbl.getCustomerId(), cbl.getDeviceSerial());
            if (!dyn.empty()) {
                games = std::move(dyn);
                selectedGameIndex = 0;
                g_lastStatusText = "Catalogo cargado: " + std::to_string(games.size()) + " juegos de Amazon Luna";
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
            catalogFetched = true;
        }
    };

    if (cbl.isAuthenticated()) {
        loadCatalogIfAuthenticated();
    }

    auto lastNavTime = std::chrono::steady_clock::now();
    uint32_t lastButtons = 0;

    uint32_t seq = 0;
    uint64_t frameCount = 0;
    uint8_t reportBuf[64];
    size_t reportLen = 0;

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
                auto filteredIndices = getFilteredIndices();
                if (filteredIndices.empty()) {
                    for (int i = 0; i < static_cast<int>(games.size()); ++i) filteredIndices.push_back(i);
                }
                if (selectedGameIndex >= static_cast<int>(filteredIndices.size())) {
                    selectedGameIndex = static_cast<int>(filteredIndices.size()) - 1;
                }
                if (selectedGameIndex < 0) selectedGameIndex = 0;

                // Cambio de categorias con L2 / R2
                if (pressedNow & luna::input::PAD_L2) {
                    selectedCategoryIdx = (selectedCategoryIdx + static_cast<int>(kCategories.size()) - 1) % kCategories.size();
                    selectedGameIndex = 0;
                    lastNavTime = now;
                }
                if (pressedNow & luna::input::PAD_R2) {
                    selectedCategoryIdx = (selectedCategoryIdx + 1) % kCategories.size();
                    selectedGameIndex = 0;
                    lastNavTime = now;
                }

                // Navegacion rapida por paginas con L1 / R1
                if (pressedNow & luna::input::PAD_L1) {
                    selectedGameIndex = std::max(0, selectedGameIndex - 5);
                    lastNavTime = now;
                }
                if (pressedNow & luna::input::PAD_R1) {
                    selectedGameIndex = std::min(static_cast<int>(filteredIndices.size()) - 1, selectedGameIndex + 5);
                    lastNavTime = now;
                }

                // Navegacion horizontal fluida por carrusel de juegos
                if (navElapsedMs > 160) {
                    bool moved = false;
                    if ((padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000)) {
                        if (selectedGameIndex + 1 < static_cast<int>(filteredIndices.size())) {
                            selectedGameIndex++;
                            moved = true;
                        }
                    } else if ((padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000)) {
                        if (selectedGameIndex > 0) {
                            selectedGameIndex--;
                            moved = true;
                        }
                    }
                    if (moved) {
                        lastNavTime = now;
                        // No borrar texto si contiene informacion de diagnostico o error
                        if (g_lastStatusText.find("Error") == std::string::npos &&
                            g_lastStatusText.find("Fallo") == std::string::npos &&
                            g_lastStatusText.find("HTTP") == std::string::npos &&
                            g_lastStatusText.find("Aviso") == std::string::npos) {
                            g_lastStatusText.clear();
                        }
                    }
                }

                // Carga progresiva bajo demanda de portadas visibles (max 1 por frame a 60 FPS)
                if (!filteredIndices.empty()) {
                    int visibleStart = std::max(0, selectedGameIndex - 2);
                    int visibleEnd = std::min(static_cast<int>(filteredIndices.size()), selectedGameIndex + 5);
                    for (int vi = visibleStart; vi < visibleEnd; ++vi) {
                        int realIdx = filteredIndices[vi];
                        if (games[realIdx].coverRgba.empty() && !games[realIdx].coverUrl.empty() && games[realIdx].coverUrl.rfind("http", 0) == 0) {
                            auto imgResp = luna::net::HttpClient::get(games[realIdx].coverUrl);
                            if (imgResp.ok() && !imgResp.body.empty()) {
                                int w = 0, h = 0;
                                if (luna::util::decodeImageRgba(reinterpret_cast<const uint8_t*>(imgResp.body.data()),
                                                                imgResp.body.size(), games[realIdx].coverRgba, w, h)) {
                                    games[realIdx].coverWidth = w;
                                    games[realIdx].coverHeight = h;
                                }
                            }
                            break; // 1 sola descarga por tick para no congelar el bucle a 60 FPS
                        }
                    }
                }

                // Cruz (X) -> Abrir Segunda Pantalla (Ficha de Detalle del Juego)
                if (pressedNow & luna::input::PAD_CROSS) {
                    if (!filteredIndices.empty() && selectedGameIndex >= 0 && selectedGameIndex < static_cast<int>(filteredIndices.size())) {
                        int realIdx = filteredIndices[selectedGameIndex];
                        luna::util::CrashLogger::log("[Main] Abriendo ficha de detalles para: %s (%s)",
                                                     games[realIdx].title.c_str(), games[realIdx].id.c_str());
                        sessionClient.preloadGameDetails(games[realIdx], cbl.getAccessToken(), cbl.getDeviceSerial());
                        appState = AppState::GameDetail;
                        break;
                    }
                }

                // Triangulo (Y) -> Cerrar sesion
                if (pressedNow & luna::input::PAD_TRIANGLE) {
                    luna::util::CrashLogger::log("[Main] Cerrando sesion de Amazon...");
                    cbl.logout();
                    catalogFetched = false;
                    g_catalogDiagnostic.clear();
                    g_lastStatusText.clear();
                    appState = AppState::LoginCBL;
                }

                std::vector<luna::display::GameTile> currentFiltered;
                for (int idx : filteredIndices) {
                    currentFiltered.push_back(games[idx]);
                }

                int actualRealIdx = filteredIndices.empty() ? 0 : filteredIndices[selectedGameIndex];
                std::string catalogStatus;
                if (!g_lastStatusText.empty()) {
                    catalogStatus = g_lastStatusText;
                } else if (!g_catalogDiagnostic.empty()) {
                    catalogStatus = g_catalogDiagnostic;
                } else if (padMgr.isConnected()) {
                    catalogStatus = "Listo para jugar | Pulsa (X) para ver detalles de " + games[actualRealIdx].title;
                } else {
                    catalogStatus = "Buscando mando DualSense... (pulsa el boton PS si no responde)";
                }

                display.drawCatalogScreen(
                    currentFiltered,
                    selectedGameIndex,
                    cbl.getCustomerId(),
                    catalogStatus,
                    frameCount,
                    kCategories[selectedCategoryIdx]
                );
                break;
            }

            case AppState::GameDetail: {
                auto filteredIndices = getFilteredIndices();
                int actualRealIdx = filteredIndices.empty() ? selectedGameIndex : filteredIndices[selectedGameIndex];
                if (actualRealIdx < 0 || actualRealIdx >= static_cast<int>(games.size())) {
                    appState = AppState::Catalog;
                    break;
                }

                auto& curGame = games[actualRealIdx];

                // Circulo (O) o Triangulo -> Volver al Catalogo
                if ((pressedNow & luna::input::PAD_CIRCLE) || (pressedNow & luna::input::PAD_TRIANGLE)) {
                    luna::util::CrashLogger::log("[Main] Regresando al catalogo desde ficha de detalles");
                    appState = AppState::Catalog;
                    break;
                }

                // Cruz (X) -> Iniciar Juego en Cloud
                if (pressedNow & luna::input::PAD_CROSS) {
                    std::string targetGameId = curGame.id;
                    if (targetGameId.find('-') == std::string::npos) {
                        targetGameId = "amzn1.luna.product.bef8572c-d814-4c91-b8e7-980d5c84ad16";
                    }
                    luna::util::CrashLogger::log("[Main] Iniciando sesion de juego Cloud para: %s (%s)...",
                                                 curGame.title.c_str(), targetGameId.c_str());

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
                    break;
                }

                display.drawGameDetailScreen(
                    curGame,
                    cbl.getCustomerId(),
                    g_lastStatusText.empty() ? ("Listo para jugar | Pulsa (X) para iniciar sesion Cloud") : g_lastStatusText,
                    false,
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
                    rtcClient.start();
                    luna::util::CrashLogger::logStep(7, "Hardware video & audio decoders ready, streaming active.");
                    videoStreaming = false;
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
                        appState = AppState::Catalog;
                    } else {
                        appState = AppState::Catalog;
                    }
                }

                auto filteredIndices = getFilteredIndices();
                std::vector<luna::display::GameTile> currentFiltered;
                for (int idx : filteredIndices) currentFiltered.push_back(games[idx]);

                display.drawCatalogScreen(
                    currentFiltered,
                    selectedGameIndex,
                    cbl.getCustomerId(),
                    sessionClient.getStatusMessage(),
                    frameCount,
                    kCategories[selectedCategoryIdx]
                );
                break;
            }

            case AppState::Streaming: {
                auto filteredIndices = getFilteredIndices();
                int actualRealIdx = filteredIndices.empty() ? selectedGameIndex : filteredIndices[selectedGameIndex];
                const auto& curGame = games[actualRealIdx];

                // Serializar y enviar entrada de mando al juego en formato Protobuf oficial de Luna a 60 Hz
                padMgr.serializeLunaProtobufReport(reportBuf, sizeof(reportBuf), reportLen, seq++);
                if (rtcClient.isConnected() && reportLen > 0) {
                    rtcClient.sendInputPacket(reportBuf, reportLen);
                }

                // Si se presiona [CIRCULO (O)] mientras no hay video streaming -> Cancelar conexion y volver al catalogo
                if (!videoStreaming.load() && (pressedNow & luna::input::PAD_CIRCLE)) {
                    luna::util::CrashLogger::log("[Main] Cancelando conexion WebRTC por usuario");
                    sessionClient.terminateSession(cbl.getAccessToken());
                    rtcClient.stop();
                    g_lastStatusText = "Conexion cancelada";
                    appState = AppState::Catalog;
                    break;
                }

                // Si se presiona Options + Touchpad -> Terminar stream y volver al catalogo
                if ((padState.buttons & luna::input::PAD_OPTIONS) && (padState.buttons & luna::input::PAD_TOUCH)) {
                    std::printf("[Main] Deteniendo stream WebRTC y regresando al catalogo...\n");
                    sessionClient.terminateSession(cbl.getAccessToken());
                    rtcClient.stop();
                    videoStreaming = false;
                    appState = AppState::Catalog;
                    break;
                }

                // Mientras no haya frames de video descodificados, mostrar pantalla de transmision en directo
                if (!videoStreaming.load()) {
                    const auto& info = sessionClient.getSessionInfo();
                    const auto& stats = rtcClient.getStats();
                    std::string statsStr = "Servidor Luna: " + info.publicIp + ":" + std::to_string(info.port) +
                                           "   |   RTP Video: " + std::to_string(stats.rtpPacketsReceived) +
                                           "   |   Audio: " + std::to_string(stats.audioFramesReceived) +
                                           "   |   Frames: " + std::to_string(stats.videoFramesAssembled);
                    display.drawStreamingStatusScreen(
                        curGame,
                        sessionClient.getStatusMessage().empty() ? "Conectando WebRTC a Amazon Luna Cloud..." : sessionClient.getStatusMessage(),
                        statsStr,
                        frameCount
                    );
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
    audioDecoder.shutdown();
    videoDecoder.shutdown();
    padMgr.shutdown();
    audio.shutdown();
    display.shutdown();
    luna::net::HttpClient::shutdown();

    std::printf("[Main] Cierre ordenado completado con exito.\n");
    return 0;
}
