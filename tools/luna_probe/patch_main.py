import re

with open('apps/luna-ps5/src/main.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Update kNavTabs and add modal variables
old_nav_tabs = '''    static const std::vector<std::string> kNavTabs = {
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
    int profileMenuIdx = 0;'''

new_nav_tabs = '''    static const std::vector<std::string> kNavTabs = {
        "Inicio",
        "GameNight",
        "Mi espacio",
        "Remote Play"
    };

    int activeTabIdx = 0;
    int navColIdx = 0; // 0..3 = tabs, 4 = Buscar (lupa), 5 = Perfil (B)
    int focusZone = 1; // 0 = Top Navbar, 1 = Content (Hero Rotator / Swimlanes)
    int selectedRowIdx = 0;
    std::vector<int> rowSelectedCol;
    int heroBtnIdx = 0; // 0 = Jugar ahora, 1 = Mas informacion

    bool profileMenuOpen = false;
    int profileMenuIdx = 0;

    bool faqModalOpen = false;
    int detailFaqIdx = 0;
    bool bluetoothModalOpen = false;
    bool cancelModalOpen = false;
    int cancelModalBtnIdx = 0;
    bool postSessionModalOpen = false;
    int postSessionSelectedIdx = 0;
    std::vector<luna::display::GameTile> postSessionSuggestions;'''

assert old_nav_tabs in content, "old_nav_tabs not found"
content = content.replace(old_nav_tabs, new_nav_tabs)

# 2. Update teardownStreamingSession to accept offerSuggestions
old_teardown = '''    auto teardownStreamingSession = [&](const std::string& reason) {
        luna::util::CrashLogger::log("[Main] teardownStreamingSession: %s", reason.c_str());
        rtcClient.stop();
        sessionClient.terminateSession(cbl.getAccessToken());
        clearMediaQueues();
        overlayOpen = false;
        streamingActiveFrames = 0;
        videoStreaming.store(false);
        g_lastStatusText = reason;
        appState = AppState::Catalog;
    };'''

new_teardown = '''    auto teardownStreamingSession = [&](const std::string& reason, bool offerSuggestions = false) {
        luna::util::CrashLogger::log("[Main] teardownStreamingSession: %s", reason.c_str());
        rtcClient.stop();
        sessionClient.terminateSession(cbl.getAccessToken());
        clearMediaQueues();
        overlayOpen = false;
        streamingActiveFrames = 0;
        videoStreaming.store(false);
        g_lastStatusText = reason;
        if (offerSuggestions) {
            postSessionSuggestions.clear();
            for (const auto& g : games) {
                if (g.id != activeGameForDetailAndStream.id &&
                    g.title.find("Luna Premium") == std::string::npos &&
                    g.id.find("luna_premium") == std::string::npos &&
                    g.id.find("promo_") == std::string::npos) {
                    postSessionSuggestions.push_back(g);
                    if (postSessionSuggestions.size() >= 5) break;
                }
            }
            if (!postSessionSuggestions.empty()) {
                postSessionModalOpen = true;
                postSessionSelectedIdx = 0;
                return;
            }
        }
        appState = AppState::Catalog;
    };'''

assert old_teardown in content, "old_teardown not found"
content = content.replace(old_teardown, new_teardown)

# 3. Insert postSessionModalOpen handler in main loop
old_loop_entry = '''        auto now = std::chrono::steady_clock::now();
        int navElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastNavTime).count();

        switch (appState) {'''

new_loop_entry = '''        auto now = std::chrono::steady_clock::now();
        int navElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastNavTime).count();

        if (postSessionModalOpen) {
            int numSug = std::min(static_cast<int>(postSessionSuggestions.size()), 5);
            if (navElapsedMs > 155) {
                bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000);
                bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000);
                if (moveLeft && postSessionSelectedIdx > 0) {
                    postSessionSelectedIdx--;
                    lastNavTime = now;
                } else if (moveRight && postSessionSelectedIdx + 1 < numSug) {
                    postSessionSelectedIdx++;
                    lastNavTime = now;
                }
            }
            if (pressedNow & luna::input::PAD_CROSS) {
                if (postSessionSelectedIdx >= 0 && postSessionSelectedIdx < numSug) {
                    postSessionModalOpen = false;
                    openGameDetail(postSessionSuggestions[postSessionSelectedIdx]);
                }
            } else if (pressedNow & luna::input::PAD_CIRCLE) {
                postSessionModalOpen = false;
                appState = AppState::Catalog;
            }
            display.drawPostSessionModal(postSessionSuggestions, postSessionSelectedIdx, frameCount);
            frameCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        switch (appState) {'''

assert old_loop_entry in content, "old_loop_entry not found"
content = content.replace(old_loop_entry, new_loop_entry)

# 4. Profile menu 10 options
old_profile_menu = '''            case AppState::Catalog: {
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
                } else if (searchOpen) {'''

new_profile_menu = '''            case AppState::Catalog: {
                if (profileMenuOpen) {
                    if (navElapsedMs > 155) {
                        bool moveUp = (padState.buttons & luna::input::PAD_UP) || (padState.leftY < -16000);
                        bool moveDown = (padState.buttons & luna::input::PAD_DOWN) || (padState.leftY > 16000);
                        if (moveUp) {
                            profileMenuIdx = (profileMenuIdx + 10 - 1) % 10;
                            lastNavTime = now;
                        } else if (moveDown) {
                            profileMenuIdx = (profileMenuIdx + 1) % 10;
                            lastNavTime = now;
                        }
                    }

                    if (pressedNow & luna::input::PAD_CIRCLE) {
                        profileMenuOpen = false;
                    } else if (pressedNow & luna::input::PAD_CROSS) {
                        if (profileMenuIdx == 0) {
                            g_lastStatusText = "Perfil activo: " + sessionClient.getPlayerProfile().displayName + " " + sessionClient.getPlayerProfile().discriminator;
                            profileMenuOpen = false;
                        } else if (profileMenuIdx == 1) {
                            g_lastStatusText = "Plan activo: " + sessionClient.getPlayerProfile().planStatus;
                            profileMenuOpen = false;
                        } else if (profileMenuIdx == 2) {
                            g_lastStatusText = "Distintivos y logros sincronizados con Amazon Luna.";
                            profileMenuOpen = false;
                        } else if (profileMenuIdx == 3) {
                            g_lastStatusText = "Twitch: Vinculacion activa para transmision.";
                            profileMenuOpen = false;
                        } else if (profileMenuIdx == 4) {
                            profileMenuOpen = false;
                            appState = AppState::Settings;
                            settingsSelectedIdx = 1;
                        } else if (profileMenuIdx == 5) {
                            profileMenuOpen = false;
                            appState = AppState::Settings;
                            settingsSelectedIdx = 0;
                        } else if (profileMenuIdx == 6) {
                            g_lastStatusText = "Amazon Luna v1.4.0 PS5: Soporte 1080p 60 FPS y Remote Play.";
                            profileMenuOpen = false;
                        } else if (profileMenuIdx == 7) {
                            g_lastStatusText = "Asistencia en linea: amazon.es/luna/help";
                            profileMenuOpen = false;
                        } else if (profileMenuIdx == 8) {
                            profileMenuOpen = false;
                            appState = AppState::Settings;
                            settingsSelectedIdx = 9;
                        } else if (profileMenuIdx == 9) {
                            luna::util::CrashLogger::log("[Main] Cerrando sesion desde menu de perfil...");
                            cbl.logout();
                            catalogFetched = false;
                            g_catalogDiagnostic.clear();
                            g_lastStatusText.clear();
                            appState = AppState::LoginCBL;
                            profileMenuOpen = false;
                            break;
                        }
                    }
                } else if (searchOpen) {'''

assert old_profile_menu in content, "old_profile_menu not found"
content = content.replace(old_profile_menu, new_profile_menu)

# 5. Remote Play left/right navigation and Cross actions
old_nav_code = '''                            } else if (moveLeft) {
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
                            }'''

new_nav_code = '''                            } else if (moveLeft) {
                                if (activeTabIdx == 3) {
                                    if (!rowSelectedCol.empty() && rowSelectedCol[0] > 0) {
                                        rowSelectedCol[0]--;
                                        moved = true;
                                    }
                                } else if (selectedRowIdx < static_cast<int>(rowSelectedCol.size())) {
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
                                if (activeTabIdx == 3) {
                                    if (!rowSelectedCol.empty() && rowSelectedCol[0] < 1) {
                                        rowSelectedCol[0]++;
                                        moved = true;
                                    }
                                } else if (selectedRowIdx < static_cast<int>(rowSelectedCol.size())) {
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
                            }'''

assert old_nav_code in content, "old_nav_code not found"
content = content.replace(old_nav_code, new_nav_code)

# 6. Navbar and Cross button
old_cross = '''                    // Accion con (X):
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
                                appState = AppState::Settings;
                                settingsSelectedIdx = 0;
                                break;
                            }
                        } else if (focusedGame) {'''

new_cross = '''                    // Accion con (X):
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
                        } else if (activeTabIdx == 3) {
                            int rpCard = (!rowSelectedCol.empty()) ? rowSelectedCol[0] : 0;
                            if (rpCard == 0) {
                                std::string pin, partyId;
                                bool ok = sessionClient.createParty(pin, partyId, cbl.getAccessToken(), cbl.getDeviceSerial());
                                if (ok) {
                                    g_lastStatusText = "Partida Remote Play creada. Codigo PIN: " + pin + " (Invita a tus amigos)";
                                } else {
                                    g_lastStatusText = sessionClient.getStatusMessage();
                                }
                            } else {
                                bool ok = sessionClient.joinParty("299983", cbl.getAccessToken(), cbl.getDeviceSerial());
                                if (ok) {
                                    g_lastStatusText = "Conectado a la sala Remote Play 299983 con exito.";
                                } else {
                                    g_lastStatusText = sessionClient.getStatusMessage();
                                }
                            }
                            break;
                        } else if (focusedGame) {'''

assert old_cross in content, "old_cross not found"
content = content.replace(old_cross, new_cross)

# 7. Update AppState::GameDetail
old_detail = '''            case AppState::GameDetail: {
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
                    } else if (detailZone == 1 && numScreens > 0 && detailScreenshotIdx >= 0 && detailScreenshotIdx < static_cast<int>(numScreens)) {
                        mediaViewerIdx = detailScreenshotIdx;
                        appState = AppState::MediaViewer;
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
            }'''

new_detail = '''            case AppState::GameDetail: {
                for (const auto& sim : currentDetailGame.similarGames) {
                    requestTileImages(sim);
                }

                if (faqModalOpen) {
                    if ((pressedNow & luna::input::PAD_CROSS) || (pressedNow & luna::input::PAD_CIRCLE)) {
                        faqModalOpen = false;
                    }
                    static const char* kFaqQuestions[] = {
                        "Que es Amazon Luna y como funciona?",
                        "Como invitar a amigos a jugar con Remote Play?",
                        "Que mandos y dispositivos son compatibles?"
                    };
                    static const char* kFaqAnswers[] = {
                        "Amazon Luna es el servicio oficial de videojuegos en la nube de Amazon. Permite jugar en streaming directo a 1080p y 60 FPS en tu PS5 a traves de servidores AWS de alto rendimiento, sin descargas, instalaciones ni esperas.",
                        "Con Remote Play de Amazon Luna puedes invitar a tus amigos a cualquier partida multijugador local o cooperativa compartiendo tu codigo unico de 6 digitos. Tus invitados no necesitan comprar el juego ni suscripcion para unirse a tu sala.",
                        "En PS5 este cliente utiliza de forma nativa los mandos inalambricos DualSense con vibracion y sticks analogicos de precision a traves de las librerias oficiales de Sony. Tambien admite el mando Amazon Luna conectado por Bluetooth."
                    };
                    int qIdx = std::clamp(detailFaqIdx, 0, 2);
                    display.drawFaqModal(kFaqQuestions[qIdx], kFaqAnswers[qIdx], frameCount);
                    break;
                }

                size_t numScreens = currentDetailGame.screenshotUrls.size();
                size_t numSimilar = currentDetailGame.similarGames.size();

                // Circulo (O) -> Volver al Catalogo
                if (pressedNow & luna::input::PAD_CIRCLE) {
                    luna::util::CrashLogger::log("[Main] Regresando al catalogo desde ficha de detalles");
                    appState = AppState::Catalog;
                    break;
                }

                // Navegacion en Ficha de Detalle (Zona 0: Botones, Zona 1: Capturas, Zona 3: FAQs, Zona 2: Similares)
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
                            } else {
                                detailZone = 3;
                                detailFaqIdx = 0;
                                moved = true;
                            }
                        } else if (detailZone == 1) {
                            detailZone = 3;
                            detailFaqIdx = 0;
                            moved = true;
                        } else if (detailZone == 3 && numSimilar > 0) {
                            detailZone = 2;
                            detailSimilarIdx = 0;
                            moved = true;
                        }
                    } else if (moveUp) {
                        if (detailZone == 2) {
                            detailZone = 3;
                            detailFaqIdx = 0;
                            moved = true;
                        } else if (detailZone == 3) {
                            detailZone = (numScreens > 0) ? 1 : 0;
                            moved = true;
                        } else if (detailZone == 1) {
                            detailZone = 0;
                            moved = true;
                        }
                    } else if (moveLeft) {
                        if (detailZone == 0 && detailBtnIdx > 0) {
                            detailBtnIdx--;
                            moved = true;
                        } else if (detailZone == 1 && numScreens > 0 && detailScreenshotIdx > 0) {
                            detailScreenshotIdx--;
                            moved = true;
                        } else if (detailZone == 3 && detailFaqIdx > 0) {
                            detailFaqIdx--;
                            moved = true;
                        } else if (detailZone == 2 && detailSimilarIdx > 0) {
                            detailSimilarIdx--;
                            moved = true;
                        }
                    } else if (moveRight) {
                        if (detailZone == 0 && detailBtnIdx < 2) {
                            detailBtnIdx++;
                            moved = true;
                        } else if (detailZone == 1 && numScreens > 0 && detailScreenshotIdx + 1 < static_cast<int>(numScreens) && detailScreenshotIdx + 1 < 3) {
                            detailScreenshotIdx++;
                            moved = true;
                        } else if (detailZone == 3 && detailFaqIdx + 1 < 3) {
                            detailFaqIdx++;
                            moved = true;
                        } else if (detailZone == 2 && detailSimilarIdx + 1 < static_cast<int>(numSimilar) && detailSimilarIdx + 1 < 8) {
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
                        } else if (detailBtnIdx == 1) {
                            toggleFavorite(currentDetailGame.id);
                            saveFavorites();
                            rebuildTabCollections();
                            g_lastStatusText = isFavorite(currentDetailGame.id) ? "Juego anadido a Favoritos." : "Juego quitado de Favoritos.";
                        } else if (detailBtnIdx == 2) {
                            std::string pin, partyId;
                            bool ok = sessionClient.createParty(pin, partyId, cbl.getAccessToken(), cbl.getDeviceSerial());
                            if (ok) {
                                g_lastStatusText = "Partida Remote Play creada para " + currentDetailGame.title + ". PIN: " + pin;
                            } else {
                                g_lastStatusText = sessionClient.getStatusMessage();
                            }
                        }
                        break;
                    } else if (detailZone == 1 && numScreens > 0 && detailScreenshotIdx >= 0 && detailScreenshotIdx < static_cast<int>(numScreens)) {
                        mediaViewerIdx = detailScreenshotIdx;
                        appState = AppState::MediaViewer;
                        break;
                    } else if (detailZone == 3) {
                        faqModalOpen = true;
                        break;
                    } else if (detailZone == 2 && detailSimilarIdx >= 0 && detailSimilarIdx < static_cast<int>(numSimilar)) {
                        openGameDetail(currentDetailGame.similarGames[detailSimilarIdx]);
                        break;
                    }
                }

                int selectedItemIdxInZone = 0;
                if (detailZone == 1) selectedItemIdxInZone = detailScreenshotIdx;
                else if (detailZone == 3) selectedItemIdxInZone = detailFaqIdx;
                else if (detailZone == 2) selectedItemIdxInZone = detailSimilarIdx;

                display.drawGameDetailScreen(
                    currentDetailGame,
                    sessionClient.getPlayerProfile(),
                    g_lastStatusText.empty()
                        ? ("Listo para jugar a " + activeGameForDetailAndStream.title + " en 1080p 60 FPS")
                        : g_lastStatusText,
                    false,
                    detailZone,
                    detailBtnIdx,
                    selectedItemIdxInZone,
                    frameCount,
                    isFavorite(currentDetailGame.id)
                );
                break;
            }'''

assert old_detail in content, "old_detail not found"
content = content.replace(old_detail, new_detail)

# 8. Update AppState::Settings
old_settings = '''            case AppState::Settings: {
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
            }'''

new_settings = '''            case AppState::Settings: {
                if (bluetoothModalOpen) {
                    if ((pressedNow & luna::input::PAD_CROSS) || (pressedNow & luna::input::PAD_CIRCLE)) {
                        bluetoothModalOpen = false;
                    }
                    display.drawBluetoothGamepadModal(frameCount);
                    break;
                }

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
                        settingsSelectedIdx = (settingsSelectedIdx + 10 - 1) % 10;
                        lastNavTime = now;
                    } else if (moveDown) {
                        settingsSelectedIdx = (settingsSelectedIdx + 1) % 10;
                        lastNavTime = now;
                    } else if (moveLeft || moveRight) {
                        if (settingsSelectedIdx == 0) {
                            bluetoothModalOpen = true;
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
                    } else if (settingsSelectedIdx == 4) {
                        settings.cycleVideoQuality();
                        settings.save(settingsFilePath);
                    } else if (settingsSelectedIdx == 5) {
                        settings.cycleRegion();
                        cbl.setRegionDomain(luna::util::I18n::getCurrentRegion().amazonDomain);
                        rebuildTabCollections();
                        settings.save(settingsFilePath);
                    } else if (settingsSelectedIdx == 6) {
                        loadCatalogIfAuthenticated();
                        g_lastStatusText = "Catalogo recargado con exito.";
                    } else if (settingsSelectedIdx == 9) {
                        cbl.logout();
                        appState = AppState::LoginCBL;
                        g_lastStatusText = "Sesion cerrada. Escanea el codigo QR para volver a vincular.";
                        break;
                    }
                }

                display.drawSettingsScreen(settingsSelectedIdx, g_lastStatusText, frameCount);
                break;
            }'''

assert old_settings in content, "old_settings not found"
content = content.replace(old_settings, new_settings)

# 9. Update AppState::StartingSession with cancel modal and display.drawStartingSession
old_starting = '''            case AppState::StartingSession: {
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
            }'''

new_starting = '''            case AppState::StartingSession: {
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
                    cancelModalOpen = false;
                    rtcClient.start();
                    luna::util::CrashLogger::logStep(7, "Hardware video & audio decoders ready, streaming active.");
                    g_lastStatusText.clear();
                    appState = AppState::Streaming;
                    break;
                } else if (sessionClient.getState() == luna::session::SessionState::Failed) {
                    cancelModalOpen = false;
                    luna::util::CrashLogger::log("[Main CRITICAL] Error al iniciar sesion: %s", sessionClient.getStatusMessage().c_str());
                    std::string failMsg = sessionClient.getStatusMessage();
                    if (failMsg.find("caducada") != std::string::npos ||
                        failMsg.find("403") != std::string::npos ||
                        failMsg.find("401") != std::string::npos) {
                        luna::util::CrashLogger::log("[Main] Sesion de cuenta invalida o revocada. Reseteando a codigo QR...");
                        cbl.logout();
                        catalogFetched = false;
                        teardownStreamingSession("Sesion expirada", false);
                        appState = AppState::LoginCBL;
                    } else if (failMsg.find("not fresh") != std::string::npos ||
                               failMsg.find("re-authenticate") != std::string::npos) {
                        cbl.refreshAccessToken();
                        teardownStreamingSession("Token refrescado. Pulsa (X) para reintentar.", false);
                        appState = AppState::GameDetail;
                    } else {
                        teardownStreamingSession("Error: " + failMsg, false);
                        appState = AppState::GameDetail;
                    }
                    break;
                }

                if (cancelModalOpen) {
                    if (navElapsedMs > 155) {
                        bool moveLeft = (padState.buttons & luna::input::PAD_LEFT) || (padState.leftX < -16000);
                        bool moveRight = (padState.buttons & luna::input::PAD_RIGHT) || (padState.leftX > 16000);
                        if (moveLeft && cancelModalBtnIdx > 0) {
                            cancelModalBtnIdx--;
                            lastNavTime = now;
                        } else if (moveRight && cancelModalBtnIdx < 1) {
                            cancelModalBtnIdx++;
                            lastNavTime = now;
                        }
                    }
                    if (pressedNow & luna::input::PAD_CROSS) {
                        if (cancelModalBtnIdx == 0) {
                            cancelModalOpen = false;
                        } else {
                            cancelModalOpen = false;
                            teardownStreamingSession("Solicitud de sesion cancelada por el usuario.", false);
                            appState = AppState::GameDetail;
                            break;
                        }
                    } else if (pressedNow & luna::input::PAD_CIRCLE) {
                        cancelModalOpen = false;
                    }
                } else {
                    if ((pressedNow & luna::input::PAD_CROSS) || (pressedNow & luna::input::PAD_CIRCLE)) {
                        cancelModalOpen = true;
                        cancelModalBtnIdx = 0;
                    }
                }

                display.drawStartingSession(
                    activeGameForDetailAndStream,
                    sessionClient.getStatusMessage(),
                    frameCount,
                    cancelModalOpen,
                    cancelModalBtnIdx
                );
                break;
            }'''

assert old_starting in content, "old_starting not found"
content = content.replace(old_starting, new_starting)

# 10. Update AppState::Streaming exits to offer suggestions
old_stream_exit1 = '''                if (rtcClient.hasRemoteSessionEnded()) {
                    teardownStreamingSession("El juego ha finalizado en Amazon Luna.");
                    break;
                }'''

new_stream_exit1 = '''                if (rtcClient.hasRemoteSessionEnded()) {
                    teardownStreamingSession("El juego ha finalizado en Amazon Luna.", true);
                    break;
                }'''

assert old_stream_exit1 in content, "old_stream_exit1 not found"
content = content.replace(old_stream_exit1, new_stream_exit1)

old_stream_exit2 = '''                            } else if (overlaySelectedIdx == 2) {
                                // 2: Salir del juego a Amazon Luna
                                teardownStreamingSession("Partida finalizada.");
                                prevOverlayCombo = comboNow;
                                break;
                            }'''

new_stream_exit2 = '''                            } else if (overlaySelectedIdx == 2) {
                                // 2: Salir del juego a Amazon Luna
                                teardownStreamingSession("Partida finalizada.", true);
                                prevOverlayCombo = comboNow;
                                break;
                            }'''

assert old_stream_exit2 in content, "old_stream_exit2 not found"
content = content.replace(old_stream_exit2, new_stream_exit2)

# Write back preserving CRLF
with open('apps/luna-ps5/src/main.cpp', 'w', encoding='utf-8', newline='\r\n') as f:
    f.write(content)

print("SUCCESS: main.cpp updated successfully!")
