#!/usr/bin/env python3

path = "apps/luna-ps5/src/display/display_manager.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

content = content.replace("\r\n", "\n")

# 1. Patch Hero Banner backdrop selection
old_hero_img = """        int heroY = 0 - yOffset;
        if (heroY + 560 > 0) {
            // Fondo panoramico que ocupa todo el ancho superior con degradado suave a #0E0B16
            auto heroImg = luna::util::ImageLoader::instance().getImage(heroGame.backdropUrl, luna::util::ImagePreset::PanoramicHero);
            if (heroImg && heroImg->ready) {
                drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
            } else {
                if (!heroGame.backdropUrl.empty()) {
                    luna::util::ImageLoader::instance().requestImage(heroGame.backdropUrl, luna::util::ImagePreset::PanoramicHero);
                }
                fillRect(0, std::max(0, heroY), 1920, 540, 0xFF140F24);
            }"""

new_hero_img = """        int heroY = 0 - yOffset;
        if (heroY + 560 > 0) {
            // Fondo panoramico que ocupa todo el ancho superior con degradado suave a #0E0B16
            std::string heroBackdrop = heroGame.backdropUrl;
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
            } else {
                if (!heroBackdrop.empty()) {
                    luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                }
                fillRect(0, std::max(0, heroY), 1920, 540, 0xFF140F24);
            }"""

if old_hero_img in content:
    content = content.replace(old_hero_img, new_hero_img, 1)
    print("Hero image fallback patched!")
else:
    print("Hero image target not found!")

# 2. Patch swimlanes ranking numbers and card spacing
old_swimlanes = """        // Desplazamiento horizontal suave
        int scrollOffset = std::max(0, (selectedCol - 3) * (cardW + cardGap));

        for (size_t i = 0; i < row.items.size(); ++i) {
            int cardX = 68 + static_cast<int>(i) * (cardW + cardGap) - scrollOffset;
            if (cardX + cardW < 0 || cardX > 1920) continue;

            const auto& item = row.items[i];
            bool isSelectedCard = isFocusedRow && (static_cast<int>(i) == selectedCol);
            int cardY = rowY + 50;

            if (row.isRankings) {
                std::string rankStr = std::to_string(i + 1);
                drawText(cardX - 16, cardY + cardH - 80, 8, rankStr, 0x77484064);
                drawText(cardX - 18, cardY + cardH - 82, 8, rankStr, 0x996E6490);
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
        }"""

new_swimlanes = """        // Desplazamiento horizontal suave y margen para rankings
        int curCardGap = row.isRankings ? 20 : 24;
        int rankMargin = row.isRankings ? 55 : 0;
        int slotWidth = cardW + curCardGap + rankMargin;
        int scrollOffset = std::max(0, (selectedCol - (row.isRankings ? 4 : 3)) * slotWidth);

        for (size_t i = 0; i < row.items.size(); ++i) {
            int slotX = 68 + static_cast<int>(i) * slotWidth - scrollOffset;
            int numberX = slotX;
            int cardX = slotX + rankMargin;
            if (cardX + cardW < 0 || numberX > 1920) continue;

            const auto& item = row.items[i];
            bool isSelectedCard = isFocusedRow && (static_cast<int>(i) == selectedCol);
            int cardY = rowY + 50;

            if (row.isRankings) {
                std::string rankStr = std::to_string(i + 1);
                // Dibuja el número grande a la izquierda de la carátula sin taparlo
                drawText(numberX + 6, cardY + cardH - 58, 8, rankStr, 0x88484064);
                drawText(numberX + 4, cardY + cardH - 60, 8, rankStr, 0xFFE2E8F0);
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
        }"""

if old_swimlanes in content:
    content = content.replace(old_swimlanes, new_swimlanes, 1)
    print("Swimlanes ranking margin patched!")
else:
    print("Swimlanes target not found!")

with open(path, "w", encoding="utf-8") as f:
    f.write(content)
print("display_manager.cpp updated!")
