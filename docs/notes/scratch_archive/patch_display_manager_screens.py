with open('apps/luna-ps5/src/display/display_manager.cpp', 'r', encoding='utf-8') as f:
    content = f.read()

# 1. Update drawPillButton colors: always white background with black text
old_pill = '''void DisplayManager::drawPillButton(int x, int y, int w, int h, const std::string& text, bool focused, const std::string& subtext, bool hasPlayIcon) {
    int r = h / 2;
    uint32_t bgColor = focused ? 0xFFFFFFFF : 0xFF353047;
    uint32_t fgColor = focused ? 0xFF0E0B16 : 0xFFFFFFFF;
    uint32_t subColor = focused ? 0xFF353047 : 0xFF94A3B8;

    drawRoundedRect(x, y, w, h, r, bgColor);'''

new_pill = '''void DisplayManager::drawPillButton(int x, int y, int w, int h, const std::string& text, bool focused, const std::string& subtext, bool hasPlayIcon) {
    int r = h / 2;
    uint32_t bgColor = 0xFFFFFFFF;
    uint32_t fgColor = 0xFF0E0B16;
    uint32_t subColor = 0xFF4A455A;

    drawRoundedRect(x, y, w, h, r, bgColor);
    if (focused) {
        drawRoundedBorder(x - 3, y - 3, w + 6, h + 6, r + 3, 3, 0xFF9146FF);
    }'''

assert old_pill in content, 'old_pill not found'
content = content.replace(old_pill, new_pill)

# 2. Update drawText fontSize for scale 8 to 110px
old_scale8 = '''        case 7: fontSize = 66; break;
        case 8: fontSize = 80; break;'''

new_scale8 = '''        case 7: fontSize = 66; break;
        case 8: fontSize = 110; break;'''

assert old_scale8 in content, 'old_scale8 not found'
content = content.replace(old_scale8, new_scale8)

# 3. Update Hero Banner in drawLunaHomeScreen to ALWAYS draw kLunaPremiumHeroJpg on slide 0
old_hero_draw = '''            std::string heroBackdrop = heroGame.backdropUrl;
            if (heroBackdrop.empty()) {
                for (const auto& h : heroCol.items) {
                    if (!h.backdropUrl.empty()) { heroBackdrop = h.backdropUrl; break; }
                }
            }
            if (heroBackdrop.empty() && collections.size() > 1) {
                for (const auto& it : collections[1].items) {
                    if (!it.backdropUrl.empty()) { heroBackdrop = it.backdropUrl; break; }
                }
            }

            auto heroImg = luna::util::ImageLoader::instance().getImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
            if (heroImg && heroImg->ready) {
                drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
            } else if (!heroBannerRgba_.empty()) {
                drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
            } else {
                if (!heroBackdrop.empty()) {
                    luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                }
                fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
            }

            // Lado izquierdo del Hero (x = 68):
            // A. Logo transparente PNG (productIcon) o titulo en grande
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

            // B. Subtitulo limpio
            std::string sub = heroGame.subtitle.empty() ? "Mas de 100 juegos. Una suscripcion." : heroGame.subtitle;
            drawText(68, heroY + 310, 2, utf8SafeTruncate(sub, 54), 0xFFE2E8F0);

            // C. Boton redondeado (#FFFFFF / texto #0E0B16 cuando enfocado)
            int btnW = 280;
            int btnH = 58;
            std::string btnTxt = heroGame.buttonText.empty() ? "Mas informacion" : heroGame.buttonText;
            drawPillButton(68, heroY + 365, btnW, btnH, btnTxt, heroFocused);'''

new_hero_draw = '''            std::string heroBackdrop = heroGame.backdropUrl;
            bool isFirstSlide = (heroIdx == 0 || heroGame.title == "Luna Premium");
            if (isFirstSlide || heroBackdrop.empty()) {
                if (!heroBannerRgba_.empty()) {
                    drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
                } else {
                    fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
                }
            } else {
                auto heroImg = luna::util::ImageLoader::instance().getImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                if (heroImg && heroImg->ready) {
                    drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
                } else if (!heroBannerRgba_.empty()) {
                    drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
                } else {
                    luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
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
            if (isFirstSlide) {
                drawText(68, heroY + 140, 6, "Luna Premium", 0xFFFFFFFF);
                drawText(68, heroY + 310, 2, "Mas de 100 juegos. Una suscripcion.", 0xFFE2E8F0);
                drawPillButton(68, heroY + 365, 280, 58, "Mas informacion", heroFocused, "", false);
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
                std::string btnTxt = heroGame.buttonText.empty() ? "Jugar ahora" : heroGame.buttonText;
                drawPillButton(68, heroY + 365, 280, 58, btnTxt, heroFocused, "", true);
            }'''

assert old_hero_draw in content, 'old_hero_draw not found'
content = content.replace(old_hero_draw, new_hero_draw)

# 4. Filter promotional card out of rankings row in drawLunaHomeScreen & tune number alignment
old_rank_draw = '''        for (size_t i = 0; i < row.items.size(); ++i) {
            int slotX = 68 + static_cast<int>(i) * slotWidth - scrollOffset;
            int numberX = slotX;
            int cardX = slotX + rankMargin;
            if (cardX + cardW < 0 || numberX > 1920) continue;

            const auto& item = row.items[i];
            bool isSelectedCard = isFocusedRow && (static_cast<int>(i) == selectedCol);
            int cardY = rowY + 50;

            if (row.isRankings) {
                std::string rankStr = std::to_string(i + 1);
                // Dibuja el número gigante con tipografía estilizada y color 0x24FFFFFF
                drawText(numberX, cardY + cardH - 28, 8, rankStr, 0x24FFFFFF);
            }'''

new_rank_draw = '''        int rankNumberIndex = 1;
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
            }'''

assert old_rank_draw in content, 'old_rank_draw not found'
content = content.replace(old_rank_draw, new_rank_draw)

# And add rankNumberIndex++ at the end of the loop
old_rank_end = '''            if (isSelectedCard) {
                drawRoundedBorder(cardX - 2, cardY - 2, cardW + 4, cardH + 4, 16, 3, 0xFFFFFFFF);
            }
        }
    }'''

new_rank_end = '''            if (isSelectedCard) {
                drawRoundedBorder(cardX - 2, cardY - 2, cardW + 4, cardH + 4, 16, 3, 0xFFFFFFFF);
            }
            rankNumberIndex++;
        }
    }'''

assert old_rank_end in content, 'old_rank_end not found'
content = content.replace(old_rank_end, new_rank_end)

# 5. Background gradient in drawGameDetailScreen
old_detail_bg = '''    // 2. Fondo panoramico 16:9 completo con degradado
    auto bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
    if (bgImg && bgImg->ready) {
        drawPreScaledImage(0, 0, bgImg->rgba.data(), 1920, 1080, bgImg->width, bgImg->height);
    } else {
        if (!game.backdropUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
        } else if (!game.coverUrl.empty()) {
            luna::util::ImageLoader::instance().requestImage(game.coverUrl, luna::util::ImagePreset::PanoramicDetail);
        }
        fillRect(0, 0, 1920, 1080, 0xFF140F24);
    }'''

new_detail_bg = '''    // 2. Fondo panoramico 16:9 completo con degradado cinematografico 1:1
    auto bgImg = luna::util::ImageLoader::instance().getImage(game.backdropUrl, luna::util::ImagePreset::PanoramicDetail);
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
        for (int x = 0; x < 950; ++x) {
            float xAlpha = (x < 750) ? 1.0f - static_cast<float>(x) / 750.0f : 0.0f;
            float totalAlpha = std::min(1.0f, xAlpha * 0.95f + yAlpha * 0.95f);
            if (totalAlpha > 0.01f) {
                blendPixelSwizzled(base, x, y, 0xFF0E0B16, static_cast<uint8_t>(totalAlpha * 255.0f));
            }
        }
        if (y > 560) {
            float bAlpha = std::min(0.92f, static_cast<float>(y - 560) / 320.0f);
            for (int x = 950; x < 1920; ++x) {
                blendPixelSwizzled(base, x, y, 0xFF0E0B16, static_cast<uint8_t>(bAlpha * 255.0f));
            }
        }
    }'''

assert old_detail_bg in content, 'old_detail_bg not found'
content = content.replace(old_detail_bg, new_detail_bg)

with open('apps/luna-ps5/src/display/display_manager.cpp', 'w', encoding='utf-8') as f:
    f.write(content)

print('Successfully patched display_manager.cpp')
