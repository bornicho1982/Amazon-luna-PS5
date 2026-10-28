"""main.cpp v2: frame pacing + responsive pad repeat, search keyboard, 2-section GameDetail navigation,
profile screens, no legacy streaming status screen, black-frame luma gate, image queue priority."""


def load(p):
    return open(p, 'rb').read().decode('utf-8').replace('\r\n', '\n')


def save(p, s):
    open(p, 'wb').write(s.replace('\n', '\r\n').encode('utf-8'))


def rep(s, old, new, count=1):
    assert old in s, 'NOT FOUND: ' + old[:90]
    return s.replace(old, new, count)


def between(s, start, end, new):
    a = s.index(start)
    b = s.index(end, a)
    return s[:a] + new + s[b:]


P = 'apps/luna-ps5/src/main.cpp'
m = load(P)

# ---- thresholds / auto-repeat -------------------------------------------------
m = m.replace('< -16000', '< -13000').replace('> 16000', '> 13000')
m = m.replace('navElapsedMs > 155', 'navElapsedMs > navThresholdMs')

m = rep(m, '    Settings,\n    StartingSession,', '    Settings,\n    ProfileScreen,\n    StartingSession,')

m = rep(m, '    std::atomic<bool> videoStreaming{false};\n',
        '    std::atomic<bool> videoStreaming{false};\n    std::atomic<int64_t> firstDecodedMs{0};\n')
m = rep(m, '        videoDecoder.resetStreamState();\n        videoStreaming.store(false);\n',
        '        videoDecoder.resetStreamState();\n        videoStreaming.store(false);\n        firstDecodedMs.store(0);\n')

# luma gate: never expose black / uninitialised frames
m = rep(m, '                    videoStreaming.store(true);\n', '''                    if (!videoStreaming.load()) {
                        const int64_t nowMsV = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
                        int64_t firstMs = firstDecodedMs.load();
                        if (firstMs == 0) {
                            firstDecodedMs.store(nowMsV);
                            firstMs = nowMsV;
                        }
                        // Limited-range black is Y=16: only go live once the picture really has content
                        uint8_t maxLuma = 0;
                        uint32_t brightPx = 0;
                        for (int row = 0; row < frame.height; row += 2) {
                            const uint8_t* yr = frame.y + static_cast<size_t>(row) * static_cast<size_t>(frame.strideY);
                            for (int col = 0; col < frame.width; col += 2) {
                                const uint8_t v = yr[col];
                                if (v > maxLuma) maxLuma = v;
                                if (v > 32) ++brightPx;
                            }
                        }
                        brightPx *= 4; // 2x2 sampling
                        if ((maxLuma > 45 && brightPx >= 5000) || (nowMsV - firstMs) > 12000) {
                            videoStreaming.store(true);
                        }
                    }
''')

# ---- search state ------------------------------------------------------------
m = between(m, '    static const char* kFilterLetters[] = {', '    std::vector<luna::display::LunaCollection> tabCollections[4];', '''    std::string searchQuery;
    auto toLowerStr = [](std::string s) {
        for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };

    auto updateFilteredGames = [&]() {
        filteredGames.clear();
        const std::string q = toLowerStr(searchQuery);
        for (const auto& g : games) {
            if (g.title.find("Luna Premium") != std::string::npos ||
                g.id.find("luna_premium") != std::string::npos ||
                g.id.find("channel.") != std::string::npos ||
                g.id.find("promo_") != std::string::npos) {
                continue;
            }
            if (!q.empty() && toLowerStr(g.title).find(q) == std::string::npos) continue;
            filteredGames.push_back(g);
        }
        searchGridIdx = 0;
    };

''')

m = rep(m, '    std::vector<luna::display::GameTile> postSessionSuggestions;\n',
        '    std::vector<luna::display::GameTile> postSessionSuggestions;\n'
        '    int profileScreenKind = 0;\n    int profileScreenSel = 0;\n    int profileSubTab = 0;\n    int accountsSubTab = 0;\n')

# ---- image queue priority ------------------------------------------------------
m = rep(m, '        currentDetailGame = selectedTile;\n',
        '        luna::util::ImageLoader::instance().prioritizeOrClearPending();\n        currentDetailGame = selectedTile;\n')
m = rep(m, '''        for (const auto& shot : currentDetailGame.screenshotUrls) {
            luna::util::ImageLoader::instance().requestImage(shot, luna::util::ImagePreset::ScreenshotThumbnail);
        }''', '''        {
            size_t shotCount = 0;
            for (const auto& shot : currentDetailGame.screenshotUrls) {
                if (shotCount++ >= 5) break;
                luna::util::ImageLoader::instance().requestImage(shot, luna::util::ImagePreset::HorizontalCard16_9);
            }
        }''')
for old in [
    '                        activeTabIdx = (activeTabIdx + static_cast<int>(kNavTabs.size()) - 1) % static_cast<int>(kNavTabs.size());\n',
    '                        activeTabIdx = (activeTabIdx + 1) % static_cast<int>(kNavTabs.size());\n',
    '                                activeTabIdx = navColIdx;\n',
]:
    ind = old[:len(old) - len(old.lstrip())]
    m = rep(m, old, old + ind + 'luna::util::ImageLoader::instance().prioritizeOrClearPending();\n')

# ---- loop head / pacing ---------------------------------------------------------
m = rep(m, '    while (g_running) {\n        padMgr.poll();\n', '''    int navThresholdMs = 180;
    int navRepeatCount = 0;
    bool dirHeldPrev = false;
    auto paceFrame = [](std::chrono::steady_clock::time_point t0) {
        const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (elapsedUs < 16666) {
            std::this_thread::sleep_for(std::chrono::microseconds(16666 - elapsedUs));
        }
    };
    auto updateNavRepeat = [&](bool held, std::chrono::steady_clock::time_point nowTp) {
        if (held && lastNavTime == nowTp) {
            navThresholdMs = (navRepeatCount++ == 0) ? 180 : 65;
        }
    };

    while (g_running) {
        const auto frameStartTs = std::chrono::steady_clock::now();
        padMgr.poll();
''')
m = rep(m, '''        auto now = std::chrono::steady_clock::now();
        int navElapsedMs =''', '''        auto now = std::chrono::steady_clock::now();
        const bool dirHeldNow =
            (padState.buttons & (luna::input::PAD_UP | luna::input::PAD_DOWN | luna::input::PAD_LEFT | luna::input::PAD_RIGHT)) ||
            padState.leftX < -13000 || padState.leftX > 13000 || padState.leftY < -13000 || padState.leftY > 13000;
        if (dirHeldNow && !dirHeldPrev) {
            // Fresh direction press: react in this very frame, then 180 ms before auto-repeat, 65 ms per repeat
            lastNavTime = now - std::chrono::milliseconds(1000);
            navThresholdMs = 180;
            navRepeatCount = 0;
        }
        if (!dirHeldNow) {
            navThresholdMs = 180;
            navRepeatCount = 0;
        }
        dirHeldPrev = dirHeldNow;
        int navElapsedMs =''')
m = rep(m, '''            frameCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;''', '''            updateNavRepeat(dirHeldNow, now);
            frameCount++;
            paceFrame(frameStartTs);
            continue;''')
m = rep(m, '''        frameCount++;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }''', '''        updateNavRepeat(dirHeldNow, now);
        frameCount++;
        paceFrame(frameStartTs);
    }''')

# ---- profile menu mapping --------------------------------------------------------
m = between(m, '                        if (profileMenuIdx == 0) {\n                            g_lastStatusText = "Perfil activo: "',
            '\n                    }\n                } else if (searchOpen) {', '''                        profileMenuOpen = false;
                        luna::util::ImageLoader::instance().prioritizeOrClearPending();
                        if (profileMenuIdx <= 3) {
                            profileScreenKind = profileMenuIdx;
                            profileScreenSel = 0;
                            profileSubTab = 0;
                            appState = AppState::ProfileScreen;
                            break;
                        } else if (profileMenuIdx == 4) {
                            appState = AppState::Settings; settingsSelectedIdx = 1; accountsSubTab = 0; break;
                        } else if (profileMenuIdx == 5) {
                            appState = AppState::Settings; settingsSelectedIdx = 5; break;
                        } else if (profileMenuIdx == 6) {
                            appState = AppState::Settings; settingsSelectedIdx = 0; break;
                        } else if (profileMenuIdx == 7) {
                            g_lastStatusText = "Novedades de Luna: consulta las ultimas incorporaciones en amazon.es/luna";
                        } else if (profileMenuIdx == 8) {
                            appState = AppState::Settings; settingsSelectedIdx = 8; break;
                        } else if (profileMenuIdx == 9) {
                            appState = AppState::Settings; settingsSelectedIdx = 9; break;
                        }''')

# ---- search input -------------------------------------------------------------------
m = between(m, '                } else if (searchOpen) {\n', '\n                } else {\n                    // Triangulo', '''                } else if (searchOpen) {
                    static const char* kKeyChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
                    if (pressedNow & luna::input::PAD_CIRCLE) {
                        if (searchZone == 1) {
                            searchZone = 0;
                        } else {
                            searchOpen = false;
                        }
                    }
                    if (pressedNow & luna::input::PAD_SQUARE) {
                        if (!searchQuery.empty()) {
                            searchQuery.pop_back();
                            updateFilteredGames();
                        }
                    }

                    if (searchZone == 0) {
                        if (navElapsedMs > navThresholdMs) {
                            bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -13000);
                            bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 13000);
                            bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -13000);
                            bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 13000);
                            if (moveLeft) {
                                searchLetterIdx = (searchLetterIdx + 27) % 28;
                                lastNavTime = now;
                            } else if (moveRight) {
                                searchLetterIdx = (searchLetterIdx + 1) % 28;
                                lastNavTime = now;
                            } else if (moveUp) {
                                if (searchLetterIdx >= 14) searchLetterIdx -= 14;
                                lastNavTime = now;
                            } else if (moveDown) {
                                if (searchLetterIdx < 14) {
                                    searchLetterIdx += 14;
                                } else if (!filteredGames.empty()) {
                                    searchZone = 1;
                                    searchGridIdx = 0;
                                }
                                lastNavTime = now;
                            }
                        }
                        if (pressedNow & luna::input::PAD_CROSS) {
                            if (searchLetterIdx < 26) {
                                if (searchQuery.size() < 40) searchQuery.push_back(kKeyChars[searchLetterIdx]);
                            } else if (searchLetterIdx == 26) {
                                if (!searchQuery.empty() && searchQuery.back() != ' ') searchQuery.push_back(' ');
                            } else if (!searchQuery.empty()) {
                                searchQuery.pop_back();
                            }
                            updateFilteredGames();
                        }
                    } else if (searchZone == 1) {
                        int numF = static_cast<int>(filteredGames.size());
                        if (navElapsedMs > navThresholdMs) {
                            bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -13000);
                            bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 13000);
                            bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -13000);
                            bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 13000);
                            if (moveLeft && searchGridIdx > 0) {
                                searchGridIdx--;
                                lastNavTime = now;
                            } else if (moveRight && searchGridIdx + 1 < numF) {
                                searchGridIdx++;
                                lastNavTime = now;
                            } else if (moveUp) {
                                if (searchGridIdx >= 5) {
                                    searchGridIdx -= 5;
                                } else {
                                    searchZone = 0;
                                }
                                lastNavTime = now;
                            } else if (moveDown && searchGridIdx + 5 < numF) {
                                searchGridIdx += 5;
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
                    }''')
m = rep(m, '''                                searchLetterIdx = 0;
                                updateFilteredGames();''', '''                                searchLetterIdx = 0;
                                searchQuery.clear();
                                luna::util::ImageLoader::instance().prioritizeOrClearPending();
                                updateFilteredGames();''')
m = rep(m, '                display.drawLunaHomeScreen(', '                display.setSearchQuery(searchQuery);\n                display.drawLunaHomeScreen(')

# ---- GameDetail (2 sections) --------------------------------------------------------
m = between(m, '            case AppState::GameDetail: {', '            case AppState::MediaViewer: {', r'''            case AppState::GameDetail: {
                for (const auto& sim : currentDetailGame.similarGames) {
                    requestTileImages(sim);
                }

                const size_t numScreens = std::min<size_t>(5, currentDetailGame.screenshotUrls.size());
                const size_t numSimilar = std::min<size_t>(10, currentDetailGame.similarGames.size());

                if (faqModalOpen) {
                    if ((pressedNow & luna::input::PAD_CROSS) || (pressedNow & luna::input::PAD_CIRCLE)) {
                        faqModalOpen = false;
                    }
                    static const char* kFaqQuestions[3] = {
                        "¿Qué es Amazon Luna?",
                        "¿Qué incluye la suscripción Prime?",
                        "¿Qué es la suscripción Luna Premium?"
                    };
                    static const char* kFaqAnswers[3] = {
                        "Amazon Luna es el servicio de videojuegos en la nube de Amazon. Juegas en streaming a 1080p y 60 FPS desde servidores de alto rendimiento, sin descargas ni instalaciones, directamente en tu PS5.",
                        "Con tu suscripción a Amazon Prime accedes a Luna Standard: una selección de juegos incluidos sin coste adicional, con streaming a 1080p y 60 FPS y modo Remote Play con amigos.",
                        "Luna Premium es la suscripción mensual de 9,99 €/mes con más de 100 juegos en streaming, sin descargas y con Remote Play, disponible en todos tus dispositivos compatibles."
                    };
                    int qIdx = std::clamp(detailFaqIdx, 0, 2);
                    display.deferNextPresent();
                    display.drawGameDetailScreen(currentDetailGame, sessionClient.getPlayerProfile(), g_lastStatusText,
                                                 false, 3, detailBtnIdx, detailFaqIdx, frameCount,
                                                 isFavorite(currentDetailGame.id));
                    display.drawFaqModal(kFaqQuestions[qIdx], kFaqAnswers[qIdx], frameCount);
                    break;
                }

                // Circulo (O) -> Volver al Catalogo
                if (pressedNow & luna::input::PAD_CIRCLE) {
                    luna::util::CrashLogger::log("[Main] Regresando al catalogo desde ficha de detalles");
                    luna::util::ImageLoader::instance().prioritizeOrClearPending();
                    appState = AppState::Catalog;
                    break;
                }

                // Zonas: 0 botones, 1 capturas (vista superior) | 3 FAQs, 2 similares (vista inferior con scroll)
                if (navElapsedMs > navThresholdMs) {
                    bool moved = false;
                    bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -13000);
                    bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 13000);
                    bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -13000);
                    bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 13000);

                    if (moveDown) {
                        if (detailZone == 0) {
                            if (numScreens > 0) { detailZone = 1; detailScreenshotIdx = 0; }
                            else { detailZone = 3; detailFaqIdx = 0; }
                            moved = true;
                        } else if (detailZone == 1) {
                            detailZone = 3; detailFaqIdx = 0; moved = true;
                        } else if (detailZone == 3) {
                            if (detailFaqIdx < 2) { detailFaqIdx++; moved = true; }
                            else if (numSimilar > 0) { detailZone = 2; detailSimilarIdx = 0; moved = true; }
                        }
                    } else if (moveUp) {
                        if (detailZone == 2) {
                            detailZone = 3; detailFaqIdx = 2; moved = true;
                        } else if (detailZone == 3) {
                            if (detailFaqIdx > 0) { detailFaqIdx--; }
                            else { detailZone = (numScreens > 0) ? 1 : 0; }
                            moved = true;
                        } else if (detailZone == 1) {
                            detailZone = 0; moved = true;
                        }
                    } else if (moveLeft) {
                        if (detailZone == 0 && detailBtnIdx > 0) { detailBtnIdx--; moved = true; }
                        else if (detailZone == 1 && detailScreenshotIdx > 0) { detailScreenshotIdx--; moved = true; }
                        else if (detailZone == 2 && detailSimilarIdx > 0) { detailSimilarIdx--; moved = true; }
                    } else if (moveRight) {
                        if (detailZone == 0 && detailBtnIdx < 2) { detailBtnIdx++; moved = true; }
                        else if (detailZone == 1 && detailScreenshotIdx + 1 < static_cast<int>(numScreens)) { detailScreenshotIdx++; moved = true; }
                        else if (detailZone == 2 && detailSimilarIdx + 1 < static_cast<int>(numSimilar)) { detailSimilarIdx++; moved = true; }
                    }
                    if (moved) lastNavTime = now;
                }

                // Triangulo -> Alternar Favorito
                if (pressedNow & luna::input::PAD_TRIANGLE) {
                    toggleFavorite(currentDetailGame.id);
                    saveFavorites();
                    rebuildTabCollections();
                    g_lastStatusText = isFavorite(currentDetailGame.id) ? "Juego anadido a Favoritos." : "Juego quitado de Favoritos.";
                }

                // Cruz (X) -> accion segun la zona enfocada
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
                        } else if (detailBtnIdx == 1) {
                            toggleFavorite(currentDetailGame.id);
                            saveFavorites();
                            rebuildTabCollections();
                            g_lastStatusText = isFavorite(currentDetailGame.id) ? "Juego anadido a Favoritos." : "Juego quitado de Favoritos.";
                        } else if (detailBtnIdx == 2) {
                            std::string pin, partyId;
                            bool ok = sessionClient.createParty(pin, partyId, cbl.getAccessToken(), cbl.getDeviceSerial());
                            g_lastStatusText = ok ? ("Partida Remote Play creada para " + currentDetailGame.title + ". PIN: " + pin)
                                                  : sessionClient.getStatusMessage();
                        }
                        break;
                    } else if (detailZone == 1 && numScreens > 0 && detailScreenshotIdx < static_cast<int>(numScreens)) {
                        mediaViewerIdx = detailScreenshotIdx;
                        appState = AppState::MediaViewer;
                        break;
                    } else if (detailZone == 3) {
                        faqModalOpen = true;
                        break;
                    } else if (detailZone == 2 && detailSimilarIdx < static_cast<int>(numSimilar)) {
                        openGameDetail(currentDetailGame.similarGames[detailSimilarIdx]);
                        break;
                    }
                }

                int zoneSel = 0;
                if (detailZone == 1) zoneSel = detailScreenshotIdx;
                else if (detailZone == 3) zoneSel = detailFaqIdx;
                else if (detailZone == 2) zoneSel = detailSimilarIdx;

                display.drawGameDetailScreen(
                    currentDetailGame,
                    sessionClient.getPlayerProfile(),
                    g_lastStatusText.empty()
                        ? ("Listo para jugar a " + activeGameForDetailAndStream.title + " en 1080p 60 FPS")
                        : g_lastStatusText,
                    false,
                    detailZone,
                    detailBtnIdx,
                    zoneSel,
                    frameCount,
                    isFavorite(currentDetailGame.id)
                );
                break;
            }

''')

# ---- Settings + ProfileScreen ----------------------------------------------------------
m = between(m, '            case AppState::Settings: {', '            case AppState::StartingSession: {', r'''            case AppState::Settings: {
                auto& settings = luna::util::AppSettings::instance();
                if (bluetoothModalOpen) {
                    if ((pressedNow & luna::input::PAD_CROSS) || (pressedNow & luna::input::PAD_CIRCLE)) {
                        bluetoothModalOpen = false;
                    }
                    display.deferNextPresent();
                    display.drawSettingsScreen(settingsSelectedIdx, g_lastStatusText, frameCount, accountsSubTab);
                    display.drawBluetoothGamepadModal(frameCount);
                    break;
                }

                if (pressedNow & luna::input::PAD_CIRCLE) {
                    settings.save(settingsFilePath);
                    appState = AppState::Catalog;
                    break;
                }
                if (settingsSelectedIdx == 1 && ((pressedNow & luna::input::PAD_L1) || (pressedNow & luna::input::PAD_R1))) {
                    accountsSubTab = 1 - accountsSubTab;
                }

                if (navElapsedMs > navThresholdMs) {
                    bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -13000);
                    bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 13000);
                    bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -13000);
                    bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 13000);

                    if (moveUp) {
                        settingsSelectedIdx = (settingsSelectedIdx + 10 - 1) % 10;
                        lastNavTime = now;
                    } else if (moveDown) {
                        settingsSelectedIdx = (settingsSelectedIdx + 1) % 10;
                        lastNavTime = now;
                    } else if (moveLeft || moveRight) {
                        if (settingsSelectedIdx == 1) {
                            accountsSubTab = 1 - accountsSubTab;
                        } else if (settingsSelectedIdx == 4) {
                            settings.cycleVideoQuality();
                            settings.save(settingsFilePath);
                        } else if (settingsSelectedIdx == 5) {
                            settings.cycleRegion();
                            cbl.setRegionDomain(luna::util::I18n::getCurrentRegion().amazonDomain);
                            rebuildTabCollections();
                            settings.save(settingsFilePath);
                        }
                        lastNavTime = now;
                    }
                }

                if (pressedNow & luna::input::PAD_CROSS) {
                    if (settingsSelectedIdx == 0) {
                        bluetoothModalOpen = true;
                    } else if (settingsSelectedIdx == 1) {
                        accountsSubTab = 1 - accountsSubTab;
                    } else if (settingsSelectedIdx == 2) {
                        cbl.logout();
                        catalogFetched = false;
                        appState = AppState::LoginCBL;
                        g_lastStatusText = "Sesion cerrada. Escanea el codigo QR para volver a vincular.";
                        break;
                    } else if (settingsSelectedIdx == 4) {
                        settings.cycleVideoQuality();
                        settings.save(settingsFilePath);
                    } else if (settingsSelectedIdx == 5) {
                        settings.cycleRegion();
                        cbl.setRegionDomain(luna::util::I18n::getCurrentRegion().amazonDomain);
                        rebuildTabCollections();
                        settings.save(settingsFilePath);
                    } else if (settingsSelectedIdx == 8) {
                        catalogFetched = false;
                        loadCatalogIfAuthenticated();
                        g_lastStatusText = "Catalogo recargado con exito.";
                    }
                }

                display.drawSettingsScreen(settingsSelectedIdx, g_lastStatusText, frameCount, accountsSubTab);
                break;
            }

            case AppState::ProfileScreen: {
                if (pressedNow & luna::input::PAD_CIRCLE) {
                    luna::util::ImageLoader::instance().prioritizeOrClearPending();
                    appState = AppState::Catalog;
                    break;
                }
                if (profileScreenKind == 2) {
                    if (pressedNow & luna::input::PAD_L1) { profileSubTab = 0; profileScreenSel = 0; }
                    if (pressedNow & luna::input::PAD_R1) { profileSubTab = 1; profileScreenSel = 0; }
                }
                if (navElapsedMs > navThresholdMs) {
                    bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -13000);
                    bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 13000);
                    bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -13000);
                    bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 13000);
                    bool moved = false;
                    if (profileScreenKind == 0) {
                        if (moveLeft && profileScreenSel == 1) { profileScreenSel = 0; moved = true; }
                        else if (moveRight && profileScreenSel == 0) { profileScreenSel = 1; moved = true; }
                        else if (moveDown && profileScreenSel < 2) { profileScreenSel = 2; moved = true; }
                        else if (moveUp && profileScreenSel == 2) { profileScreenSel = 0; moved = true; }
                    } else if (profileScreenKind == 1) {
                        if (moveLeft && profileScreenSel > 0) { profileScreenSel--; moved = true; }
                        else if (moveRight && profileScreenSel < 1) { profileScreenSel++; moved = true; }
                    } else if (profileScreenKind == 2) {
                        if (profileSubTab == 0) {
                            if (moveRight) { profileSubTab = 1; profileScreenSel = 0; moved = true; }
                        } else {
                            if (moveLeft && profileScreenSel > 0) { profileScreenSel--; moved = true; }
                            else if (moveRight && profileScreenSel < 23) { profileScreenSel++; moved = true; }
                            else if (moveUp && profileScreenSel >= 8) { profileScreenSel -= 8; moved = true; }
                            else if (moveDown && profileScreenSel + 8 < 24) { profileScreenSel += 8; moved = true; }
                            else if (moveLeft && profileScreenSel == 0) { profileSubTab = 0; moved = true; }
                        }
                    } else if (profileScreenKind == 3) {
                        if (moveUp && profileScreenSel > 0) { profileScreenSel--; moved = true; }
                        else if (moveDown && profileScreenSel < 1) { profileScreenSel++; moved = true; }
                    }
                    if (moved) lastNavTime = now;
                }
                if (pressedNow & luna::input::PAD_CROSS) {
                    if (profileScreenKind == 0 && profileScreenSel == 2) {
                        g_lastStatusText = "La edicion de perfiles se gestiona desde la app de Amazon Luna en movil o PC.";
                    } else if (profileScreenKind == 3) {
                        g_lastStatusText = "Las opciones de Twitch se gestionan desde la app de Amazon Luna en movil o PC.";
                    }
                }
                display.drawProfileScreen(profileScreenKind, profileScreenSel, profileSubTab,
                                          sessionClient.getPlayerProfile(), games, frameCount);
                break;
            }

''')

# ---- Streaming: never show the legacy status screen --------------------------------------
m = between(m, '                    std::string statsStr = "RTP Video: "', '                } else {\n                    streamingActiveFrames++;', '''                    // Same official loading screen as StartingSession until real video frames arrive
                    display.drawStartingSession(
                        curGame,
                        sessionClient.getStatusMessage().empty() ? std::string("Conectando con el servidor de juego...")
                                                                 : sessionClient.getStatusMessage(),
                        frameCount,
                        false,
                        0);
''')
m = rep(m, '''                    if ((pressedNow & luna::input::PAD_CIRCLE) || (comboNow && !prevOverlayCombo)) {
                        teardownStreamingSession("Conexion de juego cancelada.");''', '''                    if ((pressedNow & luna::input::PAD_CIRCLE) || (pressedNow & luna::input::PAD_CROSS) || (comboNow && !prevOverlayCombo)) {
                        teardownStreamingSession("Conexion de juego cancelada.");''')

save(P, m)
print('main.cpp patched OK')
