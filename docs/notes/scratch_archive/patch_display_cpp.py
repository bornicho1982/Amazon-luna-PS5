path = r'apps/luna-ps5/src/display/display_manager.cpp'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

# 1. Add include for hero_banner_asset.h and image_loader.h
inc_target = '#include "display/display_manager.h"\n'
inc_repl = '#include "display/display_manager.h"\n#include "display/hero_banner_asset.h"\n#include "util/image_loader.h"\n'
if inc_target in text:
    text = text.replace(inc_target, inc_repl, 1)

# 2. Add loadStaticHeroBanner() call in init() and implementation
init_target = '    fontRenderer_.init();\n    return true;'
init_repl = '''    fontRenderer_.init();
    loadStaticHeroBanner();
    return true;'''
if init_target in text:
    text = text.replace(init_target, init_repl, 1)

# Add loadStaticHeroBanner implementation after shutdown()
shut_target = '        std::printf("[Display] Subsistema de pantalla cerrado\\n");\n    }\n}\n'
shut_repl = '''        std::printf("[Display] Subsistema de pantalla cerrado\\n");
    }
}

void DisplayManager::loadStaticHeroBanner() {
    if (!heroBannerRgba_.empty()) return;
    int w = 0, h = 0;
    if (luna::util::decodeImageRgba(luna::assets::kLunaPremiumHeroJpg,
                                   luna::assets::kLunaPremiumHeroJpgSize,
                                   heroBannerRgba_, w, h)) {
        heroBannerWidth_ = w;
        heroBannerHeight_ = h;
        std::printf("[Display] Static Hero Banner decodificado con exito (%dx%d, %zu bytes RGBA)\\n",
                    w, h, heroBannerRgba_.size());
    }
}
'''
if shut_target in text:
    text = text.replace(shut_target, shut_repl, 1)

# 3. In drawLunaHomeScreen: use heroBannerRgba_ when heroImg is not ready or when Luna Premium
hero_target = '''            auto heroImg = luna::util::ImageLoader::instance().getImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
            if (heroImg && heroImg->ready) {
                drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
            } else {
                if (!heroBackdrop.empty()) {
                    luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                }
                fillRect(0, std::max(0, heroY), 1920, 540, 0xFF140F24);
            }'''

hero_repl = '''            auto heroImg = luna::util::ImageLoader::instance().getImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
            if (heroImg && heroImg->ready) {
                drawPreScaledImage(0, heroY, heroImg->rgba.data(), 1920, 540, heroImg->width, heroImg->height);
            } else if (!heroBannerRgba_.empty()) {
                drawPreScaledImage(0, heroY, heroBannerRgba_.data(), 1920, 540, heroBannerWidth_, heroBannerHeight_);
            } else {
                if (!heroBackdrop.empty()) {
                    luna::util::ImageLoader::instance().requestImage(heroBackdrop, luna::util::ImagePreset::PanoramicHero);
                }
                fillRect(0, std::max(0, heroY), 1920, 540, 0xFF0E0B16);
            }'''

if hero_target in text:
    text = text.replace(hero_target, hero_repl, 1)

# 4. In Swimlanes: fix curCardGap, rankMargin, slotWidth, and drawing of numbers
swim_target = '''        // Desplazamiento horizontal suave y margen para rankings
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
            }'''

swim_repl = '''        // Desplazamiento horizontal suave y margen amplio para rankings oficiales
        int curCardGap = 28;
        int rankMargin = row.isRankings ? 72 : 0;
        int slotWidth = cardW + curCardGap + rankMargin;
        int scrollOffset = std::max(0, (selectedCol - (row.isRankings ? 3 : 3)) * slotWidth);

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
                // Dibuja el número gigante con tipografía estilizada y color 0x24FFFFFF
                drawText(numberX, cardY + cardH - 28, 8, rankStr, 0x24FFFFFF);
            }'''

if swim_target in text:
    text = text.replace(swim_target, swim_repl, 1)

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)

print("DisplayManager updated with static hero banner and refined swimlane ranking layout.")
