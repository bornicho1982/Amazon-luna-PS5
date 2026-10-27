const fs = require('fs');

let content = fs.readFileSync('apps/luna-ps5/src/main.cpp', 'utf8');

// Helper to replace ignoring CRLF vs LF
function safeReplace(target, replacement) {
  let targetLF = target.replace(/\r\n/g, '\n');
  let targetCRLF = target.replace(/\r?\n/g, '\r\n');
  if (content.includes(targetCRLF)) {
    content = content.replace(targetCRLF, replacement);
    return true;
  } else if (content.includes(targetLF)) {
    content = content.replace(targetLF, replacement);
    return true;
  }
  return false;
}

// 1. AppState enum
const oldAppState = `enum class AppState {
    LoginCBL,
    Catalog,
    GameDetail,
    StartingSession,
    Streaming
};`;

const newAppState = `enum class AppState {
    LoginCBL,
    Catalog,
    GameDetail,
    MediaViewer,
    Settings,
    StartingSession,
    Streaming
};`;

console.log('Replace AppState:', safeReplace(oldAppState, newAppState));

// 2. Add variables and load settings after cbl.init
const oldCblInit = `    cbl.init(storageDir);
    if (cbl.isAuthenticated()) {
        appState = AppState::Catalog;
    } else {
        appState = AppState::LoginCBL;
    }`;

const newCblInit = `    std::string settingsFilePath = storageDir + "/settings.json";
    luna::util::AppSettings::instance().load(settingsFilePath);
    padMgr.setVibrationEnabled(luna::util::AppSettings::instance().vibrationEnabled);
    padMgr.setDeadzonePercent(luna::util::AppSettings::instance().deadzonePercent);
    cbl.setRegionDomain(luna::util::I18n::getCurrentRegion().amazonDomain);

    int mediaViewerIdx = 0;
    int settingsSelectedIdx = 0;

    cbl.init(storageDir);
    if (cbl.isAuthenticated()) {
        appState = AppState::Catalog;
    } else {
        appState = AppState::LoginCBL;
    }`;

console.log('Replace cbl.init:', safeReplace(oldCblInit, newCblInit));

// 3. In AppState::LoginCBL, handle Triangle button to change region
const oldLoginState = `            case AppState::LoginCBL: {
                cbl.tick();
                if (cbl.isAuthenticated()) {
                    luna::util::CrashLogger::log("[Main] Autenticacion CBL exitosa");
                    loadCatalogIfAuthenticated();
                    appState = AppState::Catalog;
                    break;
                }`;

const newLoginState = `            case AppState::LoginCBL: {
                cbl.tick();
                if (pressedNow & luna::input::PAD_TRIANGLE) {
                    luna::util::AppSettings::instance().cycleRegion();
                    cbl.setRegionDomain(luna::util::I18n::getCurrentRegion().amazonDomain);
                    luna::util::AppSettings::instance().save(settingsFilePath);
                    g_lastStatusText = "Region: " + std::string(luna::util::I18n::getCurrentRegion().name);
                }
                if (cbl.isAuthenticated()) {
                    luna::util::CrashLogger::log("[Main] Autenticacion CBL exitosa");
                    loadCatalogIfAuthenticated();
                    appState = AppState::Catalog;
                    break;
                }`;

console.log('Replace LoginCBL:', safeReplace(oldLoginState, newLoginState));

// 4. In rebuildTabCollections, fix Tab 2 and Tab 3
const oldTab3Logic = `        // Tab 3: Canales / Suscripciones / A-Z
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
        }`;

const newTab3Logic = `        // Tab 2: Desactivar hero rotator en Mi espacio
        for (auto& c : tabCollections[2]) {
            c.isHeroRotator = false;
        }

        // Tab 3: Remote Play oficial (Ubisoft Connect, GOG Galaxy, Electronic Arts)
        tabCollections[3] = sessionClient.getRemotePlayCollections();
        if (tabCollections[3].empty()) {
            tabCollections[3] = sessionClient.fetchRemotePlayCollections(cbl.getAccessToken(), cbl.getDeviceSerial());
        }
        for (auto& c : tabCollections[3]) {
            c.isHeroRotator = false;
        }`;

console.log('Replace Tab3Logic:', safeReplace(oldTab3Logic, newTab3Logic));

// 5. In openGameDetail, fix similarGames fallback filtering out Luna Premium
const oldSimilarFallback = `        if (currentDetailGame.similarGames.empty()) {
            for (const auto& g : games) {
                if (g.id != currentDetailGame.id) {
                    currentDetailGame.similarGames.push_back(g);
                    if (currentDetailGame.similarGames.size() >= 10) break;
                }
            }
        }`;

const newSimilarFallback = `        if (currentDetailGame.similarGames.empty()) {
            for (const auto& g : games) {
                if (g.id != currentDetailGame.id &&
                    g.title.find("Luna Premium") == std::string::npos &&
                    g.id.find("luna_premium") == std::string::npos &&
                    g.id.find("channel.") == std::string::npos &&
                    g.id.find("promo_") == std::string::npos) {
                    currentDetailGame.similarGames.push_back(g);
                    if (currentDetailGame.similarGames.size() >= 10) break;
                }
            }
        }`;

console.log('Replace SimilarFallback:', safeReplace(oldSimilarFallback, newSimilarFallback));

// 6. In startCloudGameSession, configure 1080p / 720p streamConfig
const oldStartSession = `        cbl.refreshAccessToken();

        std::string sdpOffer = rtcClient.generateSdpOffer();`;

const newStartSession = `        cbl.refreshAccessToken();

        luna::stream::StreamConfig stCfg;
        if (luna::util::AppSettings::instance().videoQuality == luna::util::VideoQuality::P720_60) {
            stCfg.targetWidth = 1280;
            stCfg.targetHeight = 720;
        } else {
            stCfg.targetWidth = 1920;
            stCfg.targetHeight = 1080;
        }
        stCfg.targetFps = 60;
        rtcClient.init(stCfg);

        std::string sdpOffer = rtcClient.generateSdpOffer();`;

console.log('Replace StartSession:', safeReplace(oldStartSession, newStartSession));

// 7. In AppState::Catalog:
// - Triangle button opens Settings
// - NavCol 6 (Avatar) opens Settings
const oldAvatarClick = `                            } else if (navColIdx == static_cast<int>(kNavTabs.size()) + 1) {
                                profileMenuOpen = true;
                                profileMenuIdx = 0;
                            }`;

const newAvatarClick = `                            } else if (navColIdx == static_cast<int>(kNavTabs.size()) + 1) {
                                appState = AppState::Settings;
                                settingsSelectedIdx = 0;
                                break;
                            }`;

console.log('Replace AvatarClick:', safeReplace(oldAvatarClick, newAvatarClick));

const oldCatalogL1R1 = `                    // L1 / R1 para cambiar de pestaña en cualquier momento
                    if (pressedNow & luna::input::PAD_L1) {`;

const newCatalogL1R1 = `                    // Triangulo (△) -> Abrir Menu de Configuracion
                    if (pressedNow & luna::input::PAD_TRIANGLE) {
                        appState = AppState::Settings;
                        settingsSelectedIdx = 0;
                        break;
                    }

                    // L1 / R1 para cambiar de pestaña en cualquier momento
                    if (pressedNow & luna::input::PAD_L1) {`;

console.log('Replace CatalogL1R1:', safeReplace(oldCatalogL1R1, newCatalogL1R1));

// 8. In AppState::GameDetail:
// Pressing (X) on Zone 1 (Trailer & Screenshots) opens MediaViewer
const oldDetailCross = `                    if (detailZone == 0) {
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
                    }`;

const newDetailCross = `                    if (detailZone == 0) {
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
                    } else if (detailZone == 1 && numScreens > 0 && detailScreenshotIdx >= 0 && detailScreenshotIdx < static_cast<int>(numScreens)) {
                        mediaViewerIdx = detailScreenshotIdx;
                        appState = AppState::MediaViewer;
                        break;
                    } else if (detailZone == simZone && detailSimilarIdx >= 0 && detailSimilarIdx < static_cast<int>(numSimilar)) {
                        openGameDetail(currentDetailGame.similarGames[detailSimilarIdx]);
                        break;
                    }`;

console.log('Replace DetailCross:', safeReplace(oldDetailCross, newDetailCross));

// 9. Add AppState::MediaViewer and AppState::Settings cases right after AppState::GameDetail
const mediaViewerAndSettingsState = `
            case AppState::MediaViewer: {
                size_t numScreens = currentDetailGame.screenshotUrls.size();
                if (pressedNow & luna::input::PAD_CIRCLE) {
                    appState = AppState::GameDetail;
                    break;
                }
                if (numScreens > 0) {
                    if ((pressedNow & luna::input::PAD_LEFT) || (pressedNow & luna::input::PAD_L1)) {
                        mediaViewerIdx = (mediaViewerIdx + static_cast<int>(numScreens) - 1) % static_cast<int>(numScreens);
                    } else if ((pressedNow & luna::input::PAD_RIGHT) || (pressedNow & luna::input::PAD_R1)) {
                        mediaViewerIdx = (mediaViewerIdx + 1) % static_cast<int>(numScreens);
                    }
                }
                display.drawMediaViewerScreen(currentDetailGame, mediaViewerIdx, frameCount);
                break;
            }

            case AppState::Settings: {
                auto& settings = luna::util::AppSettings::instance();
                if (pressedNow & luna::input::PAD_CIRCLE) {
                    settings.save(settingsFilePath);
                    appState = AppState::Catalog;
                    break;
                }

                if (navElapsedMs > 155) {
                    bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -16000);
                    bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);
                    bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000);
                    bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000);

                    if (moveUp) {
                        settingsSelectedIdx = (settingsSelectedIdx + 8 - 1) % 8;
                        lastNavTime = now;
                    } else if (moveDown) {
                        settingsSelectedIdx = (settingsSelectedIdx + 1) % 8;
                        lastNavTime = now;
                    } else if (moveLeft || moveRight) {
                        if (settingsSelectedIdx == 0) {
                            settings.cycleVideoQuality();
                        } else if (settingsSelectedIdx == 1) {
                            settings.cycleHudMode();
                        } else if (settingsSelectedIdx == 2) {
                            settings.toggleVibration();
                            padMgr.setVibrationEnabled(settings.vibrationEnabled);
                        } else if (settingsSelectedIdx == 3) {
                            settings.cycleDeadzone();
                            padMgr.setDeadzonePercent(settings.deadzonePercent);
                        } else if (settingsSelectedIdx == 4) {
                            settings.cycleRegion();
                            cbl.setRegionDomain(luna::util::I18n::getCurrentRegion().amazonDomain);
                            rebuildTabCollections();
                        }
                        settings.save(settingsFilePath);
                        lastNavTime = now;
                    }
                }

                if (pressedNow & luna::input::PAD_CROSS) {
                    if (settingsSelectedIdx == 0) {
                        settings.cycleVideoQuality();
                        settings.save(settingsFilePath);
                    } else if (settingsSelectedIdx == 1) {
                        settings.cycleHudMode();
                        settings.save(settingsFilePath);
                    } else if (settingsSelectedIdx == 2) {
                        settings.toggleVibration();
                        padMgr.setVibrationEnabled(settings.vibrationEnabled);
                        settings.save(settingsFilePath);
                    } else if (settingsSelectedIdx == 3) {
                        settings.cycleDeadzone();
                        padMgr.setDeadzonePercent(settings.deadzonePercent);
                        settings.save(settingsFilePath);
                    } else if (settingsSelectedIdx == 4) {
                        settings.cycleRegion();
                        cbl.setRegionDomain(luna::util::I18n::getCurrentRegion().amazonDomain);
                        rebuildTabCollections();
                        settings.save(settingsFilePath);
                    } else if (settingsSelectedIdx == 6) {
                        loadCatalogIfAuthenticated();
                        g_lastStatusText = "Catalogo recargado con exito.";
                    } else if (settingsSelectedIdx == 7) {
                        cbl.logout();
                        appState = AppState::LoginCBL;
                        g_lastStatusText = "Sesion cerrada. Escanea el codigo QR para volver a vincular.";
                        break;
                    }
                }

                display.drawSettingsScreen(settingsSelectedIdx, g_lastStatusText, frameCount);
                break;
            }
`;

const gameDetailEnd = `                    frameCount,
                    isFavorite(currentDetailGame.id)
                );
                break;
            }`;

console.log('Replace GameDetailEnd:', safeReplace(gameDetailEnd, gameDetailEnd + '\n' + mediaViewerAndSettingsState));

// 10. In AppState::Streaming, adjust HUD auto-hide according to hudMode setting
const oldHudCheck = `                    streamingActiveFrames++;
                    // Auto-ocultar la barra tecnica tras los primeros 3 segundos (180 frames a 60 FPS), salvo que este fijada
                    bool showHudNow = (streamingActiveFrames <= 180) || hudPinned;`;

const newHudCheck = `                    streamingActiveFrames++;
                    bool showHudNow = false;
                    if (luna::util::AppSettings::instance().hudMode == luna::util::HudMode::Always) {
                        showHudNow = true;
                    } else if (luna::util::AppSettings::instance().hudMode == luna::util::HudMode::Auto) {
                        showHudNow = (streamingActiveFrames <= 180) || hudPinned;
                    } else {
                        showHudNow = hudPinned;
                    }`;

console.log('Replace HudCheck:', safeReplace(oldHudCheck, newHudCheck));

fs.writeFileSync('apps/luna-ps5/src/main.cpp', content, 'utf8');
console.log('Wrote main.cpp');
