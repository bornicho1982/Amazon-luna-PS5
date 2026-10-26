with open('apps/luna-ps5/src/main.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Update kNavTabs
old_tabs = '''    static const std::vector<std::string> kNavTabs = {
        "Inicio",
        "GameNight",
        "Biblioteca",
        "Luna+",
        "Catálogo A-Z"
    };'''

new_tabs = '''    static const std::vector<std::string> kNavTabs = {
        "Inicio",
        "GameNight",
        "Mi espacio",
        "Remote Play [NUEVO]"
    };'''

assert old_tabs in content, 'old_tabs not found'
content = content.replace(old_tabs, new_tabs)

# 2. Update tabCollections[5] to tabCollections[4]
old_tab_coll = '''    std::vector<luna::display::LunaCollection> tabCollections[5];'''
new_tab_coll = '''    std::vector<luna::display::LunaCollection> tabCollections[4];'''
assert old_tab_coll in content, 'old_tab_coll not found'
content = content.replace(old_tab_coll, new_tab_coll)

# 3. Update tab 4 in rebuildTabCollections
old_rebuild_end = '''        // Tab 4: Catálogo A-Z (Todos los juegos únicos ordenados alfabéticamente en filas de 10)
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
        if (tabCollections[4].empty()) tabCollections[4] = tabCollections[0];'''

assert old_rebuild_end in content, 'old_rebuild_end not found'
content = content.replace(old_rebuild_end, '')

# 4. Remove launchPs5WebBrowser calls
old_boot_launch = '''    if (cbl.isAuthenticated()) {
        loadCatalogIfAuthenticated();
        launchPs5WebBrowser(localWebUrl);
    }'''

new_boot_launch = '''    if (cbl.isAuthenticated()) {
        loadCatalogIfAuthenticated();
    }'''

assert old_boot_launch in content, 'old_boot_launch not found'
content = content.replace(old_boot_launch, new_boot_launch)

old_teardown_launch = '''        g_lastStatusText = reason;
        appState = AppState::Catalog;
        launchPs5WebBrowser(localWebUrl);
    };'''

new_teardown_launch = '''        g_lastStatusText = reason;
        appState = AppState::Catalog;
    };'''

assert old_teardown_launch in content, 'old_teardown_launch not found'
content = content.replace(old_teardown_launch, new_teardown_launch)

old_login_launch = '''                    loadCatalogIfAuthenticated();
                    launchPs5WebBrowser(localWebUrl);
                    appState = AppState::Catalog;
                    break;'''

new_login_launch = '''                    loadCatalogIfAuthenticated();
                    appState = AppState::Catalog;
                    break;'''

assert old_login_launch in content, 'old_login_launch not found'
content = content.replace(old_login_launch, new_login_launch)

# 5. Restore full native C++ navigation in AppState::Catalog
old_case_catalog = '''            case AppState::Catalog: {
                if (pressedNow & luna::input::PAD_CROSS) {
                    luna::util::CrashLogger::log("[Main] [X] pulsado: reabriendo Web de Amazon Luna en PS5");
                    launchPs5WebBrowser(localWebUrl);
                } else if (pressedNow & luna::input::PAD_SQUARE) {
                    luna::util::CrashLogger::log("[Main] [CUADRADO] pulsado: desvinculando cuenta de Amazon Luna");
                    cbl.logout();
                    catalogFetched = false;
                    g_catalogDiagnostic.clear();
                    g_lastStatusText = "Cuenta desvinculada. Escanea el nuevo codigo QR para vincular.";
                    appState = AppState::LoginCBL;
                    break;
                } else if (pressedNow & luna::input::PAD_OPTIONS) {
                    luna::util::CrashLogger::log("[Main] [OPTIONS] pulsado: saliendo de la aplicacion");
                    g_running = false;
                    break;
                }

                std::string statusMsg = g_lastStatusText;
                if (statusMsg.empty()) {
                    const auto& prof = sessionClient.getPlayerProfile();
                    if (!prof.displayName.empty()) {
                        statusMsg = "Conectado a Amazon como " + prof.displayName + " | Motor nativo en espera";
                    } else {
                        statusMsg = "Conectado a Amazon Luna | Motor nativo en espera";
                    }
                }

                display.drawNativeEngineWaitingScreen(localWebUrl, lanWebUrl, statusMsg, frameCount);
                break;
            }'''

new_case_catalog = '''            case AppState::Catalog: {
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
                                        rowSelectedCol[0] = (rowSelectedCol[0] + maxC - 1) % std::max(1, maxC);
                                    } else if (rowSelectedCol[selectedRowIdx] > 0) {
                                        rowSelectedCol[selectedRowIdx]--;
                                    }
                                    moved = true;
                                }
                            } else if (moveRight) {
                                if (selectedRowIdx < static_cast<int>(rowSelectedCol.size())) {
                                    int maxC = (selectedRowIdx < numRows) ? static_cast<int>(curCols[selectedRowIdx].items.size()) : 1;
                                    if (selectedRowIdx == 0 && curCols[0].isHeroRotator) {
                                        rowSelectedCol[0] = (rowSelectedCol[0] + 1) % std::max(1, maxC);
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
                            if (isHeroSlide && focusedGame->title == "Luna Premium") {
                                if (!games.empty()) openGameDetail(games[0]);
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
            }'''

assert old_case_catalog in content, 'old_case_catalog not found'
content = content.replace(old_case_catalog, new_case_catalog)

with open('apps/luna-ps5/src/main.cpp', 'w', encoding='utf-8') as f:
    f.write(content)

print('Successfully patched main.cpp')
