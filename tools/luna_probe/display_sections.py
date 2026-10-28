import os

# New drawLunaHomeScreen
new_drawLunaHomeScreen = """void DisplayManager::drawLunaHomeScreen(const std::vector<LunaCollection>& collections,
                                        const std::vector<std::string>& navTabs,
                                        int activeTabIdx,
                                        int focusZone,
                                        int navColIdx,
                                        int selectedRowIdx,
                                        const std::vector<int>& rowSelectedCol,
                                        int heroBtnIdx,
                                        const PlayerProfileInfo& profile,
                                        bool padConnected,
                                        const std::string& statusText,
                                        uint64_t frameCount,
                                        bool profileMenuOpen,
                                        int profileMenuIdx,
                                        bool searchOpen,
                                        int searchLetterIdx,
                                        int searchGridIdx,
                                        const std::vector<GameTile>& filteredGames) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    // 1. Fondo principal oficial de Amazon Luna (#0E0B16 Cosmic Deep)
    clear(0xFF0E0B16);

    if (collections.empty() && activeTabIdx != 3) {
        drawText(68, 500, 3, "Cargando catalogo oficial de Amazon Luna...", 0xFF94A3B8);
        present();
        return;
    }

    // PESTANA 3: REMOTE PLAY
    if (activeTabIdx == 3) {
        // Fondo tematico Remote Play
        fillRect(0, 0, 1920, 1080, 0xFF0E0B16);
        drawRoundedRect(68, 100, 1784, 110, 16, 0xFF181226);
        drawRoundedBorder(68, 100, 1784, 110, 16, 1, 0xFF2D2248);

        drawText(100, 118, 5, "Remote Play", 0xFFFFFFFF);
        drawText(100, 168, 2, "Juega con tus amigos esteis donde esteis. Compartid partidas multijugador y cooperativas al instante sin coste adicional.", 0xFF94A3B8);

        int cardY = 240;
        int cardW = 860;
        int cardH = 500;

        // Tarjeta 1: Anfitrion (Crear una partida)
        int hostX = 68;
        bool hostFoc = (focusZone != 0 && (selectedRowIdx == 0 || selectedRowIdx == 1) && (!rowSelectedCol.empty() && rowSelectedCol[0] == 0));
        drawRoundedRect(hostX, cardY, cardW, cardH, 20, 0xFF140F22);
        drawRoundedBorder(hostX, cardY, cardW, cardH, 20, hostFoc ? 3 : 1, hostFoc ? 0xFFFFFFFF : 0xFF2D2248);

        drawRoundedRect(hostX + 40, cardY + 36, 140, 36, 8, 0xFF3B1B68);
        drawText(hostX + 56, cardY + 44, 2, "ANFITRION", 0xFFE9D5FF);
        drawText(hostX + 40, cardY + 92, 4, "Crear una partida", 0xFFFFFFFF);
        drawText(hostX + 40, cardY + 140, 2, "1. Genera un codigo unico de acceso de 6 digitos.", 0xFFCBD5E1);
        drawText(hostX + 40, cardY + 175, 2, "2. Enviaselo a tus amigos (hasta 8 jugadores).", 0xFFCBD5E1);
        drawText(hostX + 40, cardY + 210, 2, "3. Inicia cualquier juego compatible de tu biblioteca.", 0xFFCBD5E1);

        // Caja de codigo generado
        drawRoundedRect(hostX + 40, cardY + 265, cardW - 80, 90, 12, 0xFF22173B);
        drawRoundedBorder(hostX + 40, cardY + 265, cardW - 80, 90, 12, 1, 0xFF4C1D95);
        drawText(hostX + 60, cardY + 285, 2, "CODIGO DE ACCESO ACTUAL:", 0xFFA78BFA);
        drawText(hostX + 60, cardY + 312, 5, "2 9 9 9 8 3", 0xFFFCD34D);
        drawText(hostX + 380, cardY + 318, 2, "Sala activa lista para jugar", 0xFF10B981);

        drawPillButton(hostX + 40, cardY + 390, 380, 64, "Generar nuevo codigo", hostFoc, "", false);

        // Tarjeta 2: Invitado (Unirse a una partida)
        int guestX = 992;
        bool guestFoc = (focusZone != 0 && (selectedRowIdx == 0 || selectedRowIdx == 1) && (!rowSelectedCol.empty() && rowSelectedCol[0] == 1));
        drawRoundedRect(guestX, cardY, cardW, cardH, 20, 0xFF140F22);
        drawRoundedBorder(guestX, cardY, cardW, cardH, 20, guestFoc ? 3 : 1, guestFoc ? 0xFFFFFFFF : 0xFF2D2248);

        drawRoundedRect(guestX + 40, cardY + 36, 130, 36, 8, 0xFF064E3B);
        drawText(guestX + 56, cardY + 44, 2, "INVITADO", 0xFFA7F3D0);
        drawText(guestX + 40, cardY + 92, 4, "Unirse a una partida", 0xFFFFFFFF);
        drawText(guestX + 40, cardY + 140, 2, "Introduce el codigo de 6 digitos facilitado por el anfitrion:", 0xFFCBD5E1);

        // Cajas de digitos interactivos [ • ] [ • ] [ • ] [ • ] [ • ] [ • ]
        int pinBoxX = guestX + 40;
        int pinBoxY = cardY + 195;
        static const char* kDemoPinDigits[] = {"2", "9", "9", "9", "8", "3"};
        for (int p = 0; p < 6; ++p) {
            drawRoundedRect(pinBoxX, pinBoxY, 68, 76, 10, 0xFF22173B);
            drawRoundedBorder(pinBoxX, pinBoxY, 68, 76, 10, 2, guestFoc ? 0xFF9146FF : 0xFF374151);
            drawText(pinBoxX + 24, pinBoxY + 20, 4, kDemoPinDigits[p], 0xFFFFFFFF);
            pinBoxX += 82;
        }

        drawText(guestX + 40, cardY + 300, 2, "Pulsa (X) para validar el codigo e incorporarte a la sesion", 0xFF94A3B8);
        drawPillButton(guestX + 40, cardY + 390, 380, 64, "Unirme a la partida", guestFoc, "", true);

        // Barra inferior y navegacion
        drawText(68, 780, 3, "Juegos compatibles con Remote Play en tu suscripcion:", 0xFFFFFFFF);
        int rpGidY = 825;
        static const char* kRpTitles[] = {"The Game of Life 2", "Goat Simulator 3", "Cluedo", "Overcooked! 2", "Moving Out"};
        for (int rg = 0; rg < 5; ++rg) {
            int rx = 68 + rg * 350;
            drawRoundedRect(rx, rpGidY, 320, 90, 12, 0xFF1E1633);
            drawRoundedBorder(rx, rpGidY, 320, 90, 12, 1, 0xFF352654);
            drawText(rx + 20, rpGidY + 20, 2, kRpTitles[rg], 0xFFFFFFFF);
            drawText(rx + 20, rpGidY + 50, 1, "Multijugador local en nube", 0xFF10B981);
        }
    } else {
        int clampedRow = std::clamp(selectedRowIdx, 0, static_cast<int>(collections.size()) - 1);
        bool hasHero = (activeTabIdx != 2) && !collections.empty() && collections[0].isHeroRotator && !collections[0].items.empty();

        // Desplazamiento vertical de filas cuando se navega hacia abajo
        int yOffset = 0;
        if (clampedRow > 1) {
            yOffset = (clampedRow - 1) * 360;
        }

        // PESTANA 2: MI ESPACIO (SIN HERO BANNER, TITULO OFICIAL EN GRANDE)
        if (activeTabIdx == 2) {
            int titleY = 100 - yOffset;
            if (titleY > -40) {
                drawText(68, titleY, 6, "Mi espacio", 0xFFFFFFFF);
            }
        }

        // 2. HERO BANNER PANORAMICO 16:9 (PARA INICIO Y GAMENIGHT)
        if (hasHero) {
            const auto& heroCol = collections[0];
            int heroIdx = (!rowSelectedCol.empty()) ? rowSelectedCol[0] : 0;
            if (heroIdx < 0 || heroIdx >= static_cast<int>(heroCol.items.size())) heroIdx = 0;
            const auto& heroGame = heroCol.items[heroIdx];
            bool heroFocused = (focusZone != 0 && clampedRow == 0);

            int heroY = 0 - yOffset;
            if (heroY + 560 > 0) {
                std::string heroBackdrop = heroGame.backdropUrl.empty() ? heroGame.coverUrl : heroGame.backdropUrl;
                bool isFirstSlide = (heroCol.isHeroRotator && heroIdx == 0 && heroGame.title == "Luna Premium");
                if (isFirstSlide) {
                    if (!heroBannerRgba_.empty()) {
                        drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
                    } else {
                        fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                    }
                } else {
                    auto heroImg = luna::util::ImageLoader::instance().getImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                    if (heroImg && heroImg->ready) {
                        drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
                    } else {
                        if (!heroBackdrop.empty()) {
                            luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                        }
                        fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                    }
                }

                // Degradado oscuro suave sobre el Hero Banner
                uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
                for (int y = 0; y < 540; ++y) {
                    int screenY = heroY + y;
                    if (screenY < 0 || screenY >= 1080) continue;
                    float yAlpha = (y > 350) ? std::min(1.0f, static_cast<float>(y - 350) / 190.0f) : 0.0f;
                    for (int x = 0; x < 950; ++x) {
                        float xAlpha = (x < 700) ? 1.0f - static_cast<float>(x) / 700.0f : 0.0f;
                        float totalAlpha = std::min(1.0f, xAlpha * 0.95f + yAlpha * 0.95f);
                        if (totalAlpha > 0.01f) {
                            blendPixelSwizzled(base, x, screenY, 0xFF0E0B16, static_cast<uint8_t>(totalAlpha * 255.0f));
                        }
                    }
                    if (y > 400) {
                        float bAlpha = std::min(1.0f, static_cast<float>(y - 400) / 140.0f);
                        for (int x = 950; x < 1920; ++x) {
                            blendPixelSwizzled(base, x, screenY, 0xFF0E0B16, static_cast<uint8_t>(bAlpha * 255.0f));
                        }
                    }
                }

                // Lado izquierdo del Hero (x = 68):
                bool btn0Focused = (focusZone == 2) ? (heroBtnIdx == 0) : heroFocused;
                bool btn1Focused = (focusZone == 2 && heroBtnIdx == 1);

                // Badge de GameNight si es pestaña GameNight
                if (activeTabIdx == 1) {
                    drawRoundedRect(68, heroY + 80, 290, 32, 8, 0xFF7C3AED);
                    drawText(82, heroY + 88, 1, "[GN] USA TELEFONOS COMO MANDOS", 0xFFFFFFFF);
                }

                if (isFirstSlide) {
                    drawText(68, heroY + 140, 6, "Luna Premium", 0xFFFFFFFF);
                    drawText(68, heroY + 310, 2, "Mas de 100 juegos. Una suscripcion.", 0xFFE2E8F0);
                    drawPillButton(68, heroY + 365, 280, 58, "Mas informacion", btn0Focused, "", false);
                } else {
                    bool drawnLogo = false;
                    if (!heroGame.logoUrl.empty()) {
                        auto logoImg = luna::util::ImageLoader::instance().getImage(heroGame.logoUrl, luna::util::ImagePreset::ProductLogo);
                        if (logoImg && logoImg->ready) {
                            drawPreScaledImage(68, heroY + 115, logoImg->rgba.data(), logoImg->width, logoImg->height, logoImg->width, logoImg->height);
                            drawnLogo = true;
                        } else {
                            luna::util::ImageLoader::instance().requestImage(heroGame.logoUrl, luna::util::ImagePreset::ProductLogo);
                        }
                    }
                    if (!drawnLogo) {
                        drawText(68, heroY + 140, 6, utf8SafeTruncate(heroGame.title, 26), 0xFFFFFFFF);
                    }
                    std::string sub = heroGame.subtitle.empty() ? (heroGame.synopsis.empty() ? "Descubre grandes titulos para jugar en la nube." : heroGame.synopsis) : heroGame.subtitle;
                    drawText(68, heroY + 310, 2, utf8SafeTruncate(sub, 54), 0xFFE2E8F0);
                    drawPillButton(68, heroY + 365, 280, 58, "Jugar ahora", btn0Focused, "", true);
                    drawPillButton(364, heroY + 365, 280, 58, "Mas informacion", btn1Focused, "", false);
                }

                int totalSlides = std::min(static_cast<int>(heroCol.items.size()), 6);
                int dotX = 68;
                int dotY = heroY + 450;
                for (int s = 0; s < totalSlides; ++s) {
                    if (s == heroIdx) {
                        drawRoundedRect(dotX, dotY, 28, 6, 3, 0xFFFFFFFF);
                        dotX += 36;
                    } else {
                        drawRoundedRect(dotX, dotY, 6, 6, 3, 0x8894A3B8);
                        dotX += 14;
                    }
                }
            }
        }

        // 3. SECCION DE SWIMLANES / FILAS HORIZONTALES
        int firstRowIdx = hasHero ? 1 : 0;
        int baseRowY = (hasHero ? 515 : 180) - yOffset;

        for (size_t r = firstRowIdx; r < collections.size(); ++r) {
            int rowY = baseRowY + static_cast<int>(r - firstRowIdx) * 360;
            if (rowY + 360 < 0 || rowY > 1080) continue;

            const auto& row = collections[r];
            bool isFocusedRow = (focusZone != 0 && static_cast<int>(r) == clampedRow);
            int selectedCol = (r < rowSelectedCol.size()) ? rowSelectedCol[r] : 0;

            // CASO A: BANNER PROMOCIONAL DE PARTNERS (PARTNER_UPSELL_ROW)
            if (row.isPartnerRow) {
                int bannerW = 1784;
                int bannerH = 220;
                drawRoundedRect(68, rowY + 10, bannerW, bannerH, 16, 0xFF241B3B);
                drawRoundedBorder(68, rowY + 10, bannerW, bannerH, 16, isFocusedRow ? 3 : 1, isFocusedRow ? 0xFFFFFFFF : 0xFF4C1D95);

                drawText(100, rowY + 40, 4, row.partnerTitle, 0xFFFFFFFF);
                drawText(100, rowY + 90, 2, row.partnerDescription, 0xFFCBD5E1);
                drawPillButton(100, rowY + 140, 280, 54, row.partnerButtonText, isFocusedRow, "", false);

                // Arte a la derecha
                if (!row.partnerPromoImage.empty()) {
                    auto bImg = luna::util::ImageLoader::instance().getImage(row.partnerPromoImage, luna::util::ImagePreset::PanoramicHero);
                    if (bImg && bImg->ready) {
                        drawPreScaledImage(1300, rowY + 20, bImg->rgba.data(), 520, 200, bImg->width, bImg->height);
                    } else {
                        luna::util::ImageLoader::instance().requestImage(row.partnerPromoImage, luna::util::ImagePreset::PanoramicHero);
                    }
                }
                continue;
            }

            // CASO B: EXPLORAR CATEGORIAS (CATEGORY_CAROUSEL)
            if (row.isCategoryCarousel) {
                drawText(68, rowY + 10, 3, "Explorar categorias", 0xFFFFFFFF);
                int sqSize = 180;
                int sqGap = 24;
                int scrollOff = std::max(0, (selectedCol - 4) * (sqSize + sqGap));
                for (size_t c = 0; c < row.items.size(); ++c) {
                    int cx = 68 + static_cast<int>(c) * (sqSize + sqGap) - scrollOff;
                    if (cx + sqSize < 0 || cx > 1920) continue;
                    bool isSel = isFocusedRow && (static_cast<int>(c) == selectedCol);

                    drawRoundedRect(cx, rowY + 50, sqSize, sqSize, 14, isSel ? 0xFF9146FF : 0xFF1C152E);
                    drawRoundedBorder(cx, rowY + 50, sqSize, sqSize, 14, isSel ? 3 : 1, isSel ? 0xFFFFFFFF : 0xFF2D2248);

                    drawText(cx + 20, rowY + 120, 3, utf8SafeTruncate(row.items[c].title, 12), 0xFFFFFFFF);
                    drawText(cx + 20, rowY + 155, 1, "Ver juegos", isSel ? 0xFFFCD34D : 0xFF94A3B8);
                }
                continue;
            }

            // CASO C: JUGADOS RECIENTEMENTE (16:9 EN MI ESPACIO)
            if (row.isRecentlyPlayed16_9 || (activeTabIdx == 2 && r == 0)) {
                drawText(68, rowY + 10, 3, row.title.empty() ? "Jugados recientemente" : row.title, 0xFFFFFFFF);
                int c16W = 290;
                int c16H = 164;
                int c16Gap = 24;
                int scrollOff = std::max(0, (selectedCol - 3) * (c16W + c16Gap));

                for (size_t i = 0; i < row.items.size(); ++i) {
                    int cardX = 68 + static_cast<int>(i) * (c16W + c16Gap) - scrollOff;
                    if (cardX + c16W < 0 || cardX > 1920) continue;
                    bool isSel = isFocusedRow && (static_cast<int>(i) == selectedCol);
                    int cardY = rowY + 50;

                    const auto& item = row.items[i];
                    std::string imgUrl = item.backdropUrl.empty() ? item.coverUrl : item.backdropUrl;
                    auto cImg = luna::util::ImageLoader::instance().getImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
                    if (cImg && cImg->ready) {
                        drawPreScaledImage(cardX, cardY, cImg->rgba.data(), c16W, c16H, cImg->width, cImg->height);
                    } else {
                        if (!imgUrl.empty()) {
                            luna::util::ImageLoader::instance().requestImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
                        }
                        drawRoundedRect(cardX, cardY, c16W, c16H, 12, 0xFF241B3B);
                    }

                    if (isSel) {
                        drawRoundedBorder(cardX - 2, cardY - 2, c16W + 4, c16H + 4, 14, 3, 0xFFFFFFFF);
                    }

                    // Titulo y Fecha relativa/exacta (28 oct, 27 oct)
                    drawText(cardX, cardY + 175, 2, utf8SafeTruncate(item.title, 22), 0xFFFFFFFF);
                    std::string dateStr = item.lastPlayedDate.empty() ? "Partida reciente" : ("Ultima partida: " + item.lastPlayedDate);
                    drawText(cardX, cardY + 202, 1, dateStr, 0xFF10B981);
                }
                continue;
            }

            // CASO D: SWIMLANE ESTANDAR CON POSTERS VERTICALES (Y RANKINGS 1..6)
            drawText(68, rowY + 10, 3, row.title, 0xFFFFFFFF);

            int cardW = 200;
            int cardH = 280;
            int curCardGap = 28;
            int rankMargin = row.isRankings ? 72 : 0;
            int slotWidth = cardW + curCardGap + rankMargin;
            int scrollOffset = std::max(0, (selectedCol - 3) * slotWidth);

            int rankNumberIndex = 1;
            for (size_t i = 0; i < row.items.size(); ++i) {
                const auto& item = row.items[i];
                if (row.isRankings) {
                    if (item.title.find("Luna Premium") != std::string::npos ||
                        item.id.find("luna_premium") != std::string::npos) {
                        continue;
                    }
                }

                int slotX = 68 + static_cast<int>(rankNumberIndex - 1) * slotWidth - scrollOffset;
                int numberX = slotX;
                int cardX = slotX + rankMargin;
                if (cardX + cardW < 0 || numberX > 1920) {
                    rankNumberIndex++;
                    continue;
                }

                bool isSelectedCard = isFocusedRow && (static_cast<int>(i) == selectedCol);
                int cardY = rowY + 50;

                if (row.isRankings) {
                    std::string rankStr = std::to_string(rankNumberIndex);
                    drawText(numberX, cardY + cardH - 95, 8, rankStr, 0x35FFFFFF);
                }

                auto coverImg = luna::util::ImageLoader::instance().getImage(item.coverUrl, luna::util::ImagePreset::VerticalPoster);
                if (coverImg && coverImg->ready) {
                    drawPreScaledImage(cardX, cardY, coverImg->rgba.data(), 200, 280, coverImg->width, coverImg->height);
                } else {
                    if (!item.coverUrl.empty()) {
                        luna::util::ImageLoader::instance().requestImage(item.coverUrl, luna::util::ImagePreset::VerticalPoster);
                    }
                    drawRoundedRect(cardX, cardY, cardW, cardH, 14, 0xFF241B3B);
                    drawText(cardX + 16, cardY + 110, 2, utf8SafeTruncate(item.title, 18), 0xFFFFFFFF);
                }

                if (isSelectedCard) {
                    drawRoundedBorder(cardX - 2, cardY - 2, cardW + 4, cardH + 4, 16, 3, 0xFFFFFFFF);
                }
                rankNumberIndex++;
            }
        }
    }

    // 4. BARRA DE NAVEGACION SUPERIOR
    fillRect(0, 0, 1920, 68, 0xCC0E0B16);
    drawText(44, 22, 2, "<", 0xFF94A3B8);
    drawText(74, 22, 2, ">", 0xFF94A3B8);
    drawLunaLogoIcon(112, 22, 22);
    drawText(142, 20, 3, "luna", 0xFFFFFFFF);

    int tabX = 230;
    int numTabs = static_cast<int>(navTabs.size());
    for (int t = 0; t < numTabs; ++t) {
        bool active = (t == activeTabIdx);
        bool focusedTab = (focusZone == 0 && navColIdx == t);

        uint32_t tColor = (active || focusedTab) ? 0xFFFFFFFF : 0xFF94A3B8;
        drawText(tabX, 22, 2, navTabs[t], tColor);
        int tw = fontRenderer_.measureText(navTabs[t], 20);

        if (focusedTab) {
            drawRoundedBorder(tabX - 8, 14, tw + 16, 40, 8, 2, 0xFF9146FF);
        } else if (active) {
            fillRect(tabX, 52, tw, 3, 0xFFFFFFFF);
        }
        tabX += tw + 32;
    }

    int magX = 1754, magY = 32, magR = 8;
    bool magFocused = (focusZone == 0 && navColIdx == numTabs);
    if (magFocused) {
        drawRoundedBorder(magX - 16, 14, 38, 38, 8, 2, 0xFFFFFFFF);
    }
    drawRoundedBorder(magX - magR, magY - magR, magR * 2, magR * 2, magR, 2, magFocused ? 0xFFFFFFFF : 0xFFCBD5E1);
    for (int d = 0; d < 6; ++d) {
        fillRect(magX + magR - 3 + d, magY + magR - 3 + d, 2, 2, magFocused ? 0xFFFFFFFF : 0xFFCBD5E1);
    }

    int profX = 1810, profY = 16;
    bool profFocused = (focusZone == 0 && navColIdx == numTabs + 1);
    if (profFocused) {
        drawRoundedBorder(profX - 3, profY - 3, 44, 44, 22, 3, 0xFFFFFFFF);
    }
    drawRoundedRect(profX, profY, 38, 38, 19, 0xFF14B8A6);
    drawText(profX + 12, profY + 8, 2, "B", 0xFFFFFFFF);

    // 5. BARRA DE ESTADO INFERIOR
    if (!statusText.empty()) {
        fillRect(0, 1046, 1920, 34, 0xEE0B0912);
        drawText(44, 1054, 1, statusText, 0xFF94A3B8);
        drawText(1700, 1054, 1, "DualSense conectado", padConnected ? 0xFF10B981 : 0xFFEF4444);
    }

    // 6. POPUP MENU PERFIL DE USUARIO (10 OPCIONES OFICIALES)
    if (profileMenuOpen) {
        int pw = 480, ph = 640;
        int px = 1920 - pw - 40;
        int py = 76;
        drawRoundedRect(px, py, pw, ph, 16, 0xF8140F24);
        drawRoundedBorder(px, py, pw, ph, 16, 2, 0xFF9146FF);

        drawRoundedRect(px + 24, py + 20, 48, 48, 24, 0xFF14B8A6);
        drawText(px + 40, py + 30, 3, "B", 0xFFFFFFFF);
        drawText(px + 86, py + 22, 3, profile.displayName + " " + profile.discriminator, 0xFFFFFFFF);
        drawText(px + 86, py + 50, 1, profile.planStatus, 0xFF10B981);

        fillRect(px + 24, py + 80, pw - 48, 1, 0xFF2D2545);

        static const char* kOfficialProfileOpts[10] = {
            "Quien va a jugar en Luna? (Perfiles)",
            "Planes de Luna (Luna Premium 9,99 EUR)",
            "Mis distintivos y logros",
            "Transmision de Twitch",
            "Cuentas vinculadas (Ubisoft, EA, Epic)",
            "Ajustes de la aplicacion",
            "Novedades de Luna",
            "Ayuda y asistencia tecnica",
            "Acerca de Luna y legal (v1.8.197368.0)",
            "Cerrar sesion de Amazon"
        };

        for (int m = 0; m < 10; ++m) {
            int by = py + 92 + m * 52;
            bool btnFoc = (profileMenuIdx == m);
            if (btnFoc) {
                drawRoundedRect(px + 16, by, pw - 32, 44, 8, 0xFF9146FF);
                drawText(px + 32, by + 12, 2, kOfficialProfileOpts[m], 0xFFFFFFFF);
            } else {
                drawText(px + 32, by + 12, 2, kOfficialProfileOpts[m], 0xFFCBD5E1);
            }
        }
    }

    // 7. VISTA DE BUSQUEDA EN CUADRICULA DE 5 COLUMNAS EN 16:9
    if (searchOpen) {
        fillRect(0, 0, 1920, 1080, 0xF80B0912);
        drawRoundedRect(68, 28, 1784, 54, 10, 0xFF1C152E);
        drawRoundedBorder(68, 28, 1784, 54, 10, 2, 0xFF9146FF);
        drawText(90, 44, 2, "BUSQUEDA Y EXPLORACION DE JUEGOS EN AMAZON LUNA", 0xFFFFFFFF);
        drawText(1600, 46, 1, "(O) Volver al inicio", 0xFF94A3B8);

        static const char* kFilterLetters[] = {
            "TODOS", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
            "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "#"
        };
        int letX = 68;
        int letY = 96;
        for (int l = 0; l < 28; ++l) {
            bool letFoc = (searchLetterIdx == l);
            int lw = fontRenderer_.measureText(kFilterLetters[l], 18) + 16;
            if (letFoc) {
                drawRoundedRect(letX, letY, lw, 36, 6, 0xFF9146FF);
                drawText(letX + 8, letY + 8, 2, kFilterLetters[l], 0xFFFFFFFF);
            } else {
                drawText(letX + 8, letY + 8, 2, kFilterLetters[l], 0xFF94A3B8);
            }
            letX += lw + 6;
        }

        // Cuadricula de 5 columnas en 16:9 (290x164 px)
        int gBaseX = 68;
        int gBaseY = 150;
        int gCardW = 290;
        int gCardH = 164;
        int gGapX = 35;
        int gGapY = 70;
        int numFiltered = static_cast<int>(filteredGames.size());
        for (int g = 0; g < numFiltered && g < 15; ++g) {
            int col = g % 5;
            int row = g / 5;
            int gx = gBaseX + col * (gCardW + gGapX);
            int gy = gBaseY + row * (gCardH + gGapY);
            bool gFoc = (searchGridIdx == g);

            const auto& tile = filteredGames[g];
            std::string imgUrl = tile.backdropUrl.empty() ? tile.coverUrl : tile.backdropUrl;
            auto cImg = luna::util::ImageLoader::instance().getImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
            if (cImg && cImg->ready) {
                drawPreScaledImage(gx, gy, cImg->rgba.data(), gCardW, gCardH, cImg->width, cImg->height);
            } else {
                if (!imgUrl.empty()) {
                    luna::util::ImageLoader::instance().requestImage(imgUrl, luna::util::ImagePreset::HorizontalCard16_9);
                }
                drawRoundedRect(gx, gy, gCardW, gCardH, 12, 0xFF241B3B);
            }
            if (gFoc) {
                drawRoundedBorder(gx - 2, gy - 2, gCardW + 4, gCardH + 4, 14, 3, 0xFFFFFFFF);
            }
            drawText(gx, gy + 172, 2, utf8SafeTruncate(tile.title, 20), 0xFFFFFFFF);
            drawText(gx, gy + 196, 1, utf8SafeTruncate(tile.genres, 24), 0xFF94A3B8);
        }
    }

    present();
}
"""

print(f"new_drawLunaHomeScreen size: {len(new_drawLunaHomeScreen)}")

new_drawGameDetailScreen = """void DisplayManager::drawGameDetailScreen(const GameTile& game,
                                          const PlayerProfileInfo& profile,
                                          const std::string& statusText,
                                          bool isConnecting,
                                          int detailZone,
                                          int detailBtnIdx,
                                          int similarSelectedIdx,
                                          uint64_t frameCount,
                                          bool isFavorite) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    // 1. Fondo principal oficial
    clear(0xFF0E0B16);

    // 2. Fondo panoramico 16:9 completo con degradado cinematografico 1:1
    auto bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
    if (!bgImg || !bgImg->ready) {
        bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicHero);
    }
    if (bgImg && bgImg->ready) {
        drawPreScaledImage(0, 0, bgImg->rgba.data(), 1920, 1080, bgImg->width, bgImg->height);
    } else {
        if (!game.backdropUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
        } else if (!game.coverUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(game.coverUrl, luna::util::ImagePreset::PanoramicDetail);
        }
        fillRect(0, 0, 1920, 1080, 0xFF140F24);
    }

    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    for (int y = 0; y < 1080; ++y) {
        float yAlpha = (y > 480) ? std::min(1.0f, static_cast<float>(y - 480) / 380.0f) : 0.0f;
        for (int x = 0; x < 1050; ++x) {
            float xAlpha = (x < 850) ? 1.0f - static_cast<float>(x) / 850.0f : 0.0f;
            float totalAlpha = std::min(1.0f, xAlpha * 0.95f + yAlpha * 0.95f);
            if (totalAlpha > 0.01f) {
                blendPixelSwizzled(base, x, y, 0xFF0E0B16, static_cast<uint8_t>(totalAlpha * 255.0f));
            }
        }
        if (y > 520) {
            float bAlpha = std::min(0.92f, static_cast<float>(y - 520) / 340.0f);
            for (int x = 1050; x < 1920; ++x) {
                blendPixelSwizzled(base, x, y, 0xFF0E0B16, static_cast<uint8_t>(bAlpha * 255.0f));
            }
        }
    }

    // Barra superior: < Volver
    fillRect(0, 0, 1920, 68, 0x990E0B16);
    drawText(44, 22, 2, "<  Volver al catalogo", 0xFFE2E8F0);
    int magX = 1754, magY = 32, magR = 8;
    drawRoundedBorder(magX - magR, magY - magR, magR * 2, magR * 2, magR, 2, 0xFFCBD5E1);
    for (int d = 0; d < 6; ++d) {
        fillRect(magX + magR - 3 + d, magY + magR - 3 + d, 2, 2, 0xFFCBD5E1);
    }
    drawRoundedRect(1810, 16, 38, 38, 19, 0xFF14B8A6);
    drawText(1822, 24, 2, "B", 0xFFFFFFFF);

    // 3. COLUMNA IZQUIERDA (x = 68): Ficha y Sinopsis
    bool drawnLogo = false;
    if (!game.logoUrl.empty()) {
        auto logoImg = luna::util::ImageLoader::instance().getImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
        if (logoImg && logoImg->ready) {
            drawPreScaledImage(68, 120, logoImg->rgba.data(), logoImg->width, logoImg->height, logoImg->width, logoImg->height);
            drawnLogo = true;
        } else {
            luna::util::ImageLoader::instance().requestImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
        }
    }
    if (!drawnLogo) {
        drawText(68, 140, 6, utf8SafeTruncate(game.title, 26), 0xFFFFFFFF);
    }

    std::string genreText = game.genres.empty() ? "Accion, Aventura" : game.genres;
    drawText(68, 290, 2, genreText, 0xFFA0A0B0);

    // Botones de accion oficiales:
    // [ ▶ Jugar ahora ]  [ ★ ]  [ 👥 Remote Play ]
    int btnH = 60;
    bool primFocused = (detailZone == 0 && detailBtnIdx == 0);
    bool favFocused = (detailZone == 0 && detailBtnIdx == 1);
    bool rpFocused = (detailZone == 0 && detailBtnIdx == 2);

    if (game.isEntitled) {
        std::string label = isConnecting ? "CONECTANDO..." : "Jugar ahora";
        drawPillButton(68, 330, 260, btnH, label, primFocused, "", true);
    } else {
        std::string pri = game.buttonText.empty() ? "Incluido en Luna Premium" : game.buttonText;
        drawPillButton(68, 330, 260, btnH, pri, primFocused, "9,99 EUR/mes");
    }

    // Boton de Favoritos con estrella
    drawRoundedRect(340, 330, 60, btnH, 14, favFocused ? 0xFFFFFFFF : 0xFF2D2248);
    uint32_t favStarColor = isFavorite ? 0xFFF59E0B : (favFocused ? 0xFF0E0B16 : 0xFFFFFFFF);
    drawStarIcon(370, 360, 12, favStarColor);

    // Boton Remote Play
    drawRoundedRect(412, 330, 200, btnH, 14, rpFocused ? 0xFF9146FF : 0xFF2D2248);
    drawRoundedBorder(412, 330, 200, btnH, 14, rpFocused ? 2 : 1, rpFocused ? 0xFFFFFFFF : 0xFF4C1D95);
    drawText(432, 350, 2, "Remote Play", 0xFFFFFFFF);

    // Sinopsis multilineas oficial
    auto synLines = utf8WrapLines(game.synopsis, 75, 4);
    int synY = 415;
    for (const auto& l : synLines) {
        drawText(68, synY, 2, l, 0xFFCBD5E1);
        synY += 24;
    }

    // Galeria de Trailers y Capturas
    size_t numScreens = game.screenshotUrls.size();
    if (numScreens > 0) {
        drawText(68, 545, 3, "Trailer y capturas de pantalla", 0xFFFFFFFF);
        int thumbW = 250, thumbH = 140, thumbGap = 16;
        int thumbY = 585;
        for (size_t t = 0; t < numScreens && t < 3; ++t) {
            int tx = 68 + static_cast<int>(t) * (thumbW + thumbGap);
            bool isTrailer = (t == 0);
            std::string imgUrl = game.screenshotUrls[t];

            auto thImg = luna::util::ImageLoader::instance().getImage(imgUrl, luna::util::ImagePreset::ScreenshotThumbnail);
            if (thImg && thImg->ready) {
                drawPreScaledImage(tx, thumbY, thImg->rgba.data(), thumbW, thumbH, thImg->width, thImg->height);
            } else {
                luna::util::ImageLoader::instance().requestImage(imgUrl, luna::util::ImagePreset::ScreenshotThumbnail);
                drawRoundedRect(tx, thumbY, thumbW, thumbH, 10, 0xFF241B3B);
            }
            if (isTrailer) {
                drawRoundedRect(tx + (thumbW - 48) / 2, thumbY + (thumbH - 48) / 2, 48, 48, 24, 0xCC0E0B16);
                drawPlayTriangleIcon(tx + thumbW / 2, thumbY + thumbH / 2, 10, 0xFFFFFFFF);
            }
            if (detailZone == 1 && similarSelectedIdx == static_cast<int>(t)) {
                drawRoundedBorder(tx - 2, thumbY - 2, thumbW + 4, thumbH + 4, 12, 3, 0xFFFFFFFF);
            }
        }
    }

    // FAQs oficiales
    int faqY = 750;
    drawText(68, faqY, 3, "Preguntas frecuentes", 0xFFFFFFFF);
    static const char* kFaqTitles[] = {
        "[?] Que es Amazon Luna y como funciona?",
        "[?] Como invitar a amigos a jugar con Remote Play?",
        "[?] Que mandos y dispositivos son compatibles?"
    };
    for (int q = 0; q < 3; ++q) {
        int qy = faqY + 36 + q * 42;
        bool qFoc = (detailZone == 3 && similarSelectedIdx == q);
        drawRoundedRect(68, qy, 800, 36, 8, qFoc ? 0xFF9146FF : 0xFF1C152E);
        drawText(82, qy + 10, 2, kFaqTitles[q], qFoc ? 0xFFFFFFFF : 0xFFCBD5E1);
    }

    // 4. COLUMNA DERECHA (x = 1100): Tabla tecnica oficial de especificaciones
    int tabX = 1120;
    int tabY = 135;
    int tabW = 730;
    int tabH = 650;
    drawRoundedRect(tabX, tabY, tabW, tabH, 16, 0xEE161024);
    drawRoundedBorder(tabX, tabY, tabW, tabH, 16, 1, 0xFF2D2248);

    drawText(tabX + 32, tabY + 28, 4, "Especificaciones tecnicas", 0xFFFFFFFF);
    fillRect(tabX + 32, tabY + 68, tabW - 64, 1, 0xFF352654);

    struct SpecRow { const char* label; std::string value; };
    std::vector<SpecRow> specs = {
        {"Fecha de lanzamiento", game.releaseYear.empty() ? "2025" : game.releaseYear},
        {"Editor", utf8SafeTruncate(game.publisher.empty() ? "Amazon Luna" : game.publisher, 30)},
        {"Desarrollador", utf8SafeTruncate(game.developer.empty() ? "Amazon Games" : game.developer, 30)},
        {"Modos de juego", game.playerCount.empty() ? "1 jugador" : game.playerCount},
        {"Calidad de video", "Hasta 1080p a 60 fps (Full HD)"},
        {"Mandos compatibles", "Mando DualSense PS5, Mando Luna, Teclado"},
        {"Idiomas disponibles", "Espanol (Espana), Ingles, Frances, Aleman"},
        {"Calificacion por edades", game.pegiRating.empty() ? "PEGI 12" : game.pegiRating}
    };

    int sY = tabY + 84;
    for (const auto& sp : specs) {
        drawText(tabX + 32, sY, 2, sp.label, 0xFF94A3B8);
        drawText(tabX + 320, sY, 2, sp.value, 0xFFFFFFFF);
        sY += 46;
    }

    // PEGI rating visual dentro de la tabla
    int pegiBoxY = sY + 10;
    uint32_t pegiBg = 0xFFF59E0B;
    std::string pegiNum = "12";
    if (game.pegiRating.find("18") != std::string::npos) { pegiBg = 0xFFDC2626; pegiNum = "18"; }
    else if (game.pegiRating.find("16") != std::string::npos) { pegiBg = 0xFFEA580C; pegiNum = "16"; }
    else if (game.pegiRating.find("7") != std::string::npos || game.pegiRating.find("3") != std::string::npos) { pegiBg = 0xFF10B981; pegiNum = "7"; }

    drawRoundedRect(tabX + 32, pegiBoxY, 60, 68, 8, pegiBg);
    drawText(tabX + 46, pegiBoxY + 8, 1, "PEGI", 0xFF0E0B16);
    drawText(tabX + 42, pegiBoxY + 28, 3, pegiNum, 0xFF0E0B16);
    drawText(tabX + 108, pegiBoxY + 24, 2, "Apto segun normativa PEGI europea", 0xFFCBD5E1);

    // 5. Carrusel inferior "Tambien te puede interesar"
    if (!game.similarGames.empty()) {
        int simY = 890;
        drawText(68, simY, 3, "Tambien te puede interesar", 0xFFFFFFFF);
        int simCardW = 120, simCardH = 168, simGap = 20;
        int simCardsY = simY + 34;
        for (size_t s = 0; s < game.similarGames.size() && s < 10; ++s) {
            int sx = 68 + static_cast<int>(s) * (simCardW + simGap);
            if (sx + simCardW > 1920) break;
            const auto& simTile = game.similarGames[s];
            auto sImg = luna::util::ImageLoader::instance().getImage(simTile.coverUrl, luna::util::ImagePreset::VerticalPoster);
            if (sImg && sImg->ready) {
                drawPreScaledImage(sx, simCardsY, sImg->rgba.data(), simCardW, simCardH, sImg->width, sImg->height);
            } else {
                if (!simTile.coverUrl.empty()) {
                    luna::util::ImageLoader::instance().requestImage(simTile.coverUrl, luna::util::ImagePreset::VerticalPoster);
                }
                drawRoundedRect(sx, simCardsY, simCardW, simCardH, 8, 0xFF241B3B);
            }
            if (detailZone == 2 && similarSelectedIdx == static_cast<int>(s)) {
                drawRoundedBorder(sx - 2, simCardsY - 2, simCardW + 4, simCardH + 4, 10, 3, 0xFFFFFFFF);
            }
        }
    }

    // 6. Barra de estado inferior
    if (!statusText.empty()) {
        fillRect(0, 1046, 1920, 34, 0xEE0B0912);
        drawText(44, 1054, 1, statusText, 0xFF94A3B8);
    }

    present();
}
"""

new_drawMediaViewerScreen = """void DisplayManager::drawMediaViewerScreen(const GameTile& game,
                                          int currentMediaIdx,
                                          uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    clear(0xFF08060D);

    size_t numScreens = game.screenshotUrls.size();
    if (numScreens == 0) {
        drawText(68, 500, 3, "No hay capturas disponibles para este juego.", 0xFFFFFFFF);
        present();
        return;
    }

    if (currentMediaIdx < 0) currentMediaIdx = 0;
    if (currentMediaIdx >= static_cast<int>(numScreens)) currentMediaIdx = static_cast<int>(numScreens) - 1;

    std::string currentUrl = game.screenshotUrls[currentMediaIdx];

    // Carga robusta: Si no esta en 1080p aun, muestra la miniatura escalada al instante sin bloquear la pantalla
    auto img = luna::util::ImageLoader::instance().getImage(currentUrl, luna::util::ImagePreset::PanoramicDetail);
    if (!img || !img->ready) {
        img = luna::util::ImageLoader::instance().getImage(currentUrl, luna::util::ImagePreset::ScreenshotThumbnail);
    }

    if (img && img->ready) {
        drawPreScaledImage(0, 0, img->rgba.data(), 1920, 1080, img->width, img->height);
    } else {
        luna::util::ImageLoader::instance().requestImage(currentUrl, luna::util::ImagePreset::PanoramicDetail);
        luna::util::ImageLoader::instance().requestImage(currentUrl, luna::util::ImagePreset::ScreenshotThumbnail);
        fillRect(0, 0, 1920, 1080, 0xFF140F22);
    }

    // Pre-cargar imagenes adyacentes para navegacion instantanea
    if (currentMediaIdx + 1 < static_cast<int>(numScreens)) {
        luna::util::ImageLoader::instance().requestImage(game.screenshotUrls[currentMediaIdx + 1], luna::util::ImagePreset::PanoramicDetail);
    }
    if (currentMediaIdx > 0) {
        luna::util::ImageLoader::instance().requestImage(game.screenshotUrls[currentMediaIdx - 1], luna::util::ImagePreset::PanoramicDetail);
    }

    // Degradados superior e inferior para legibilidad
    fillRect(0, 0, 1920, 90, 0xCC0E0B16);
    fillRect(0, 990, 1920, 90, 0xCC0E0B16);

    drawText(68, 30, 4, game.title, 0xFFFFFFFF);
    std::string countStr = (currentMediaIdx == 0 && !game.videoUrl.empty())
        ? "Trailer oficial"
        : ("Captura " + std::to_string(currentMediaIdx + 1) + " de " + std::to_string(numScreens));
    drawText(1600, 32, 3, countStr, 0xFFFCD34D);

    std::string ctrlHint = "[◀ / L1] Anterior      [▶ / R1] Siguiente      [(O)] Volver a la ficha del juego";
    drawText(68, 1025, 2, ctrlHint, 0xFFE2E8F0);

    present();
}
"""

new_drawSettingsScreen = """void DisplayManager::drawSettingsScreen(int selectedItemIdx,
                                        const std::string& statusText,
                                        uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    clear(0xFF0E0B16);

    // Barra superior
    fillRect(0, 0, 1920, 90, 0xFF181226);
    fillRect(0, 90, 1920, 2, 0xFF9146FF);
    drawLunaLogoIcon(50, 24, 42);
    drawText(105, 30, 4, "Ajustes y Configuracion de Amazon Luna", 0xFFFFFFFF);
    drawText(1580, 36, 2, "[(O) Volver al inicio]", 0xFFCBD5E1);

    auto& s = luna::util::AppSettings::instance();

    // 10 Secciones oficiales de Ajustes
    struct SettingSection {
        const char* title;
        const char* desc;
        const char* icon;
    };
    static const SettingSection kSections[10] = {
        {"Mandos y Gamepads", "DualSense PS5, Mando Luna, Bluetooth", "[M]"},
        {"Cuentas vinculadas", "Ubisoft, EA, Epic Games, GOG, Twitch", "[C]"},
        {"Tu cuenta", "Jesus (Bornicho #8663) - Luna Estandar", "[U]"},
        {"Control parental", "Bloqueo por edades PEGI y PIN de 5 digitos", "[P]"},
        {"Calidad de audio y video", "1080p 60 FPS (Recomendado) o 720p", "[Q]"},
        {"Idioma y region", "13 idiomas disponibles (Espanol, English...)", "[L]"},
        {"Datos de partidas guardadas", "Sincronizacion en nube Amazon Cloud Save", "[S]"},
        {"Descargar Luna", "Disponible en PC, Mac, Fire TV, iOS y Android", "[D]"},
        {"Ayuda y sugerencias", "Diagnostico de conexion y soporte tecnico", "[H]"},
        {"Acerca de Luna y legal", "Version de cliente v1.8.197368.0", "[A]"}
    };

    int startY = 115;
    int optW = 750;
    int optH = 78;
    int optGap = 12;

    for (int i = 0; i < 10; ++i) {
        int y = startY + i * (optH + optGap);
        if (y + optH > 1050) break;
        bool focused = (selectedItemIdx == i);

        uint32_t bg = focused ? 0xFF9146FF : 0xFF1C152E;
        uint32_t border = focused ? 0xFFFFFFFF : 0xFF2D2248;
        drawRoundedRect(50, y, optW, optH, 12, bg);
        drawRoundedBorder(50, y, optW, optH, 12, focused ? 3 : 1, border);

        drawRoundedRect(68, y + 18, 42, 42, 8, focused ? 0xFF7C3AED : 0xFF281E42);
        drawText(78, y + 28, 2, kSections[i].icon, focused ? 0xFFFFFFFF : 0xFFA78BFA);

        drawText(124, y + 16, 2, kSections[i].title, 0xFFFFFFFF);
        drawText(124, y + 44, 1, kSections[i].desc, focused ? 0xFFFCD34D : 0xFF94A3B8);

        if (focused) {
            drawText(optW - 10, y + 28, 2, "▶", 0xFFFFFFFF);
        }
    }

    // Panel de detalle a la derecha (x = 830, w = 1040, h = 900)
    int pX = 830;
    int pW = 1040;
    int pH = 910;
    drawRoundedRect(pX, startY, pW, pH, 16, 0xFF161024);
    drawRoundedBorder(pX, startY, pW, pH, 16, 1, 0xFF2D2248);

    drawText(pX + 40, startY + 32, 4, kSections[selectedItemIdx].title, 0xFFFFFFFF);
    fillRect(pX + 40, startY + 74, pW - 80, 1, 0xFF9146FF);

    int dY = startY + 95;
    if (selectedItemIdx == 0) {
        // Mandos y Gamepads
        drawText(pX + 40, dY, 3, "Mandos actualmente conectados:", 0xFFFFFFFF);
        drawRoundedRect(pX + 40, dY + 40, pW - 80, 100, 12, 0xFF22173B);
        drawText(pX + 65, dY + 60, 3, "Mando inalambrico DualSense PS5", 0xFFFFFFFF);
        drawText(pX + 65, dY + 95, 2, "Bateria: 100% • Respuesta nativa: 0 ms • Vibracion activa", 0xFF10B981);

        drawText(pX + 40, dY + 180, 3, "Mando oficial Amazon Luna:", 0xFFFFFFFF);
        drawText(pX + 40, dY + 220, 2, "Puedes conectar un mando Amazon Luna via Cloud Direct o Bluetooth.", 0xFFCBD5E1);
        drawPillButton(pX + 40, dY + 265, 380, 60, "Instrucciones de emparejamiento", true, "", false);
    } else if (selectedItemIdx == 1) {
        // Cuentas vinculadas
        drawText(pX + 40, dY, 3, "Proveedores y tiendas de juegos vinculadas:", 0xFFFFFFFF);
        static const char* kAccounts[] = {
            "Ubisoft Connect (Vinculada - Cuenta activa)",
            "Epic Games Store (Vinculada - Cuenta activa)",
            "Electronic Arts / EA Play (Vinculada)",
            "GOG Galaxy (Vinculada)",
            "Twitch (bornicho - Vinculada)"
        };
        for (int a = 0; a < 5; ++a) {
            int ay = dY + 45 + a * 65;
            drawRoundedRect(pX + 40, ay, pW - 80, 52, 10, 0xFF1E1633);
            drawText(pX + 60, ay + 16, 2, kAccounts[a], 0xFFFFFFFF);
            drawText(pX + pW - 200, ay + 16, 2, "[DESVINCULAR]", 0xFF94A3B8);
        }
    } else if (selectedItemIdx == 3) {
        // Control parental
        drawText(pX + 40, dY, 3, "Ajustes de restriccion por edad (PEGI):", 0xFFFFFFFF);
        drawText(pX + 40, dY + 40, 2, "Todos los juegos que superen el limite fijado requeriran el PIN.", 0xFFCBD5E1);
        drawRoundedRect(pX + 40, dY + 80, pW - 80, 70, 10, 0xFF22173B);
        drawText(pX + 60, dY + 102, 3, "Nivel de bloqueo actual: DESACTIVADO (Acceso libre)", 0xFF10B981);
        drawPillButton(pX + 40, dY + 175, 300, 58, "Establecer PIN de 5 digitos", true, "", false);
    } else if (selectedItemIdx == 4) {
        // Calidad de audio y video
        drawText(pX + 40, dY, 3, "Resolucion y tasa de fotogramas:", 0xFFFFFFFF);
        drawRoundedRect(pX + 40, dY + 40, pW - 80, 80, 10, 0xFF22173B);
        drawText(pX + 60, dY + 58, 3, "1080p a 60 FPS (Full HD recomendada)", 0xFFFFFFFF);
        drawText(pX + 60, dY + 85, 1, "Consumo aproximado: 10 GB/hora • Maxima nitidez", 0xFF94A3B8);

        drawRoundedRect(pX + 40, dY + 135, pW - 80, 80, 10, 0xFF1C152E);
        drawText(pX + 60, dY + 153, 3, "720p a 60 FPS (Bajo consumo)", 0xFFCBD5E1);
        drawText(pX + 60, dY + 180, 1, "Consumo aproximado: 5 GB/hora • Menor uso de datos", 0xFF94A3B8);
    } else if (selectedItemIdx == 5) {
        // Idioma y region
        drawText(pX + 40, dY, 3, "Idioma de la aplicacion y catalogo:", 0xFFFFFFFF);
        static const char* kLangs[] = {
            "Espanol (Espana) [ACTIVO]", "English (United States)", "English (United Kingdom)",
            "Deutsch (Deutschland)", "Francais (France)", "Italiano (Italia)",
            "Polski", "Nederlands", "Portugues (Brasil)", "Espanol (Latinoamerica)"
        };
        for (int l = 0; l < 8; ++l) {
            int ly = dY + 45 + l * 54;
            drawRoundedRect(pX + 40, ly, pW - 80, 44, 8, l == 0 ? 0xFF7C3AED : 0xFF1E1633);
            drawText(pX + 60, ly + 12, 2, kLangs[l], 0xFFFFFFFF);
        }
    } else {
        // Seccion general
        drawText(pX + 40, dY, 2, kSections[selectedItemIdx].desc, 0xFFCBD5E1);
        drawText(pX + 40, dY + 40, 2, "Amazon Luna Client v1.8.197368.0", 0xFF94A3B8);
        drawText(pX + 40, dY + 70, 2, "Servidores europeos Tempo Proxy activos en eu-west-1", 0xFF10B981);
    }

    if (!statusText.empty()) {
        fillRect(0, 1046, 1920, 34, 0xEE0B0912);
        drawText(44, 1054, 1, statusText, 0xFF94A3B8);
    }

    present();
}
"""

new_helpers = """void DisplayManager::drawStartingSession(const GameTile& game,
                                         const std::string& statusMsg,
                                         uint64_t frameCount,
                                         bool cancelModalOpen,
                                         int cancelModalBtnIdx) {
    if (!isReady_ || !mappedMemory_) return;

    clear(0xFF07050A);

    // Fondo oscurecido con imagen del juego
    auto bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
    if (!bgImg || !bgImg->ready) {
        bgImg = luna::util::ImageLoader::instance().getImage(game.coverUrl, luna::util::ImagePreset::PanoramicDetail);
    }
    if (bgImg && bgImg->ready) {
        drawPreScaledImage(0, 0, bgImg->rgba.data(), 1920, 1080, bgImg->width, bgImg->height);
    }

    // Vinetado oscuro radial sobre el fondo
    uint8_t* base = static_cast<uint8_t*>(mappedMemory_) + (currentBuffer_ * kFrameBytes);
    for (int y = 0; y < 1080; ++y) {
        for (int x = 0; x < 1920; ++x) {
            blendPixelSwizzled(base, x, y, 0xFF07050A, 210);
        }
    }

    // PEGI rating arriba a la izquierda
    drawRoundedRect(68, 50, 60, 68, 8, 0xFFF59E0B);
    drawText(82, 58, 1, "PEGI", 0xFF0E0B16);
    drawText(78, 78, 3, "12", 0xFF0E0B16);

    // Boton [(X) Cancelar] arriba a la derecha
    drawRoundedRect(1650, 50, 200, 50, 10, 0xFF241B3B);
    drawRoundedBorder(1650, 50, 200, 50, 10, 1, 0xFF4C1D95);
    drawText(1680, 64, 2, "[(X) Cancelar]", 0xFFE2E8F0);

    // Logotipo o Titulo grande centrado
    bool drawnLogo = false;
    if (!game.logoUrl.empty()) {
        auto logoImg = luna::util::ImageLoader::instance().getImage(game.logoUrl, luna::util::ImagePreset::ProductLogo);
        if (logoImg && logoImg->ready) {
            int lx = (1920 - logoImg->width) / 2;
            int ly = 380;
            drawPreScaledImage(lx, ly, logoImg->rgba.data(), logoImg->width, logoImg->height, logoImg->width, logoImg->height);
            drawnLogo = true;
        }
    }
    if (!drawnLogo) {
        int tw = fontRenderer_.measureText(game.title, 48);
        drawText((1920 - tw) / 2, 430, 6, game.title, 0xFFFFFFFF);
    }

    // Spinner oficial de 3 puntos orbitando abajo a la derecha
    int spinX = 1480;
    int spinY = 960;
    int phase = static_cast<int>((frameCount / 6) % 3);
    for (int p = 0; p < 3; ++p) {
        uint32_t dotColor = (p == phase) ? 0xFFFFFFFF : 0x6694A3B8;
        int dotR = (p == phase) ? 7 : 5;
        drawRoundedRect(spinX + p * 24, spinY - dotR, dotR * 2, dotR * 2, dotR, dotColor);
    }
    std::string msg = statusMsg.empty() ? "Iniciando tu sesion de juego en la nube..." : statusMsg;
    drawText(spinX - 440, spinY - 6, 2, msg, 0xFFCBD5E1);

    // Modal de confirmacion de cancelacion si esta abierto
    if (cancelModalOpen) {
        fillRect(0, 0, 1920, 1080, 0xCC000000);
        int mw = 840, mh = 320;
        int mx = (1920 - mw) / 2, my = (1080 - mh) / 2;
        drawRoundedRect(mx, my, mw, mh, 16, 0xFF181226);
        drawRoundedBorder(mx, my, mw, mh, 16, 2, 0xFF9146FF);

        drawText(mx + 50, my + 45, 4, "Salir del juego y abandonar la cola?", 0xFFFFFFFF);
        drawText(mx + 50, my + 105, 2, "Perderas tu posicion actual en la cola de servidores de Amazon Luna.", 0xFFCBD5E1);

        bool btn0Foc = (cancelModalBtnIdx == 0);
        bool btn1Foc = (cancelModalBtnIdx == 1);
        drawPillButton(mx + 50, my + 180, 340, 60, "Continuar en la cola", btn0Foc, "", false);
        drawPillButton(mx + 420, my + 180, 340, 60, "Salir del juego", btn1Foc, "", false);
    }

    present();
}

void DisplayManager::drawPostSessionModal(const std::vector<GameTile>& suggestions,
                                         int selectedSuggestionIdx,
                                         uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    fillRect(0, 0, 1920, 1080, 0xDD07050A);

    int mw = 1440, mh = 640;
    int mx = (1920 - mw) / 2, my = (1080 - mh) / 2;
    drawRoundedRect(mx, my, mw, mh, 20, 0xFF140F22);
    drawRoundedBorder(mx, my, mw, mh, 20, 2, 0xFF9146FF);

    drawText(mx + 60, my + 45, 4, "Juegos divertidos para seguir jugando...", 0xFFFFFFFF);
    drawText(mx + 60, my + 92, 2, "Recomendaciones basadas en tu sesion en Amazon Luna", 0xFF94A3B8);

    int cW = 220, cH = 310, cGap = 24;
    int startX = mx + 60;
    int startY = my + 145;

    int numSug = std::min(static_cast<int>(suggestions.size()), 5);
    for (int i = 0; i < numSug; ++i) {
        int cx = startX + i * (cW + cGap);
        bool isFoc = (selectedSuggestionIdx == i);

        const auto& sTile = suggestions[i];
        auto cImg = luna::util::ImageLoader::instance().getImage(sTile.coverUrl, luna::util::ImagePreset::VerticalPoster);
        if (cImg && cImg->ready) {
            drawPreScaledImage(cx, startY, cImg->rgba.data(), cW, cH, cImg->width, cImg->height);
        } else {
            drawRoundedRect(cx, startY, cW, cH, 12, 0xFF241B3B);
        }
        if (isFoc) {
            drawRoundedBorder(cx - 2, startY - 2, cW + 4, cH + 4, 14, 3, 0xFFFFFFFF);
        }
        drawText(cx, startY + cH + 16, 2, utf8SafeTruncate(sTile.title, 18), 0xFFFFFFFF);
    }

    drawPillButton(mx + (mw - 320) / 2, my + mh - 90, 320, 56, "[(O) Volver al inicio]", true, "", false);
    present();
}

void DisplayManager::drawFaqModal(const std::string& faqQuestion,
                                 const std::string& faqAnswer,
                                 uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    fillRect(0, 0, 1920, 1080, 0xD0000000);

    int mw = 1200, mh = 520;
    int mx = (1920 - mw) / 2, my = (1080 - mh) / 2;
    drawRoundedRect(mx, my, mw, mh, 16, 0xFF181226);
    drawRoundedBorder(mx, my, mw, mh, 16, 2, 0xFF9146FF);

    drawText(mx + 60, my + 45, 4, faqQuestion, 0xFFFFFFFF);
    fillRect(mx + 60, my + 95, mw - 120, 1, 0xFF352654);

    auto ansLines = utf8WrapLines(faqAnswer, 85, 8);
    int ay = my + 125;
    for (const auto& al : ansLines) {
        drawText(mx + 60, ay, 2, al, 0xFFCBD5E1);
        ay += 32;
    }

    drawPillButton(mx + (mw - 340) / 2, my + mh - 90, 340, 56, "[(X) Entendido / Cerrar]", true, "", false);
    present();
}

void DisplayManager::drawBluetoothGamepadModal(uint64_t frameCount) {
    if (!isReady_ || !mappedMemory_) return;
    (void)frameCount;

    fillRect(0, 0, 1920, 1080, 0xD0000000);

    int mw = 1000, mh = 480;
    int mx = (1920 - mw) / 2, my = (1080 - mh) / 2;
    drawRoundedRect(mx, my, mw, mh, 16, 0xFF181226);
    drawRoundedBorder(mx, my, mw, mh, 16, 2, 0xFF9146FF);

    drawText(mx + 50, my + 40, 4, "Emparejar Mando Amazon Luna por Bluetooth", 0xFFFFFFFF);
    fillRect(mx + 50, my + 85, mw - 100, 1, 0xFF9146FF);

    drawText(mx + 50, my + 120, 2, "1. Asegurate de que el mando Luna tiene pilas nuevas o bateria cargada.", 0xFFCBD5E1);
    drawText(mx + 50, my + 165, 2, "2. Manten pulsados el boton de accion (circulo) y el boton de inicio durante 3 segundos.", 0xFFCBD5E1);
    drawText(mx + 50, my + 210, 2, "3. El anillo luminoso naranja empezara a parpadear en modo emparejamiento.", 0xFFCBD5E1);
    drawText(mx + 50, my + 255, 2, "4. Tu consola PS5 o pantalla detectara el mando automaticamente.", 0xFF10B981);

    drawPillButton(mx + (mw - 280) / 2, my + mh - 90, 280, 56, "[(X) Entendido]", true, "", false);
    present();
}
"""
