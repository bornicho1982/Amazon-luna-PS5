#!/usr/bin/env python3

path = "apps/luna-ps5/src/session/luna_session.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

content = content.replace("\r\n", "\n")

# 1. Update cleanGid check in parseGameTileFromWidget to allow rotator slides
old_cleanGid = """    if (cleanGid.empty()) return false;"""

new_cleanGid = """    if (cleanGid.empty()) {
        if (colId.find("featured_rotator") != std::string::npos || swType.find("ROTATOR") != std::string::npos) {
            cleanGid = swId.empty() ? ("promo_" + std::to_string(colorIdx)) : swId;
        } else {
            return false;
        }
    }"""

if old_cleanGid in content:
    content = content.replace(old_cleanGid, new_cleanGid, 1)
    print("cleanGid rotator fallback patched!")
else:
    print("cleanGid target not found!")

# 2. Update coverUrl and backdropUrl extraction in parseGameTileFromWidget
old_images = """    std::string coverUrl = pdata["imagePortrait"].asString();
    if (coverUrl.empty()) coverUrl = pdata["tileImagePortrait"].asString();
    if (coverUrl.empty()) coverUrl = pdata["focusedImagePortrait"].asString();
    if (coverUrl.empty()) coverUrl = pdata["boxArtUrl"].asString();
    if (coverUrl.empty()) coverUrl = pdata["tileImageLandscape"].asString();
    if (coverUrl.empty()) coverUrl = pdata["imageLandscape"].asString();

    std::string backdropUrl = pdata["focusedImageLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["focusedImage"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["backgroundFeatureLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["backgroundImageLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["tileImageLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["imageLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["backgroundImage"].asString();"""

new_images = """    std::string coverUrl = pdata["imagePortrait"].asString();
    if (coverUrl.empty()) coverUrl = pdata["tileImagePortrait"].asString();
    if (coverUrl.empty()) coverUrl = pdata["focusedImagePortrait"].asString();
    if (coverUrl.empty()) coverUrl = pdata["backgroundFeaturePortrait"].asString();
    if (coverUrl.empty()) coverUrl = pdata["boxArtUrl"].asString();
    if (coverUrl.empty()) coverUrl = pdata["tileImageLandscape"].asString();
    if (coverUrl.empty()) coverUrl = pdata["imageLandscape"].asString();
    if (coverUrl.empty()) coverUrl = pdata["imageUrl"].asString();
    if (coverUrl.empty()) coverUrl = sw["imageUrl"].asString();

    std::string backdropUrl = pdata["focusedImageLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["backgroundFeatureLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["focusedImage"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["backgroundImage"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["backgroundImageLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["unfocusedImageLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["tileImageLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["imageLandscape"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["imageUrl"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["heroImageUrl"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["bannerUrl"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["artUrl"].asString();
    if (backdropUrl.empty()) backdropUrl = pdata["promoImageUrl"].asString();
    if (backdropUrl.empty()) backdropUrl = sw["backgroundImage"].asString();
    if (backdropUrl.empty()) backdropUrl = sw["imageUrl"].asString();
    if (backdropUrl.empty()) backdropUrl = coverUrl;"""

if old_images in content:
    content = content.replace(old_images, new_images, 1)
    print("images extraction patched!")
else:
    print("images target not found!")

# 3. Add fallback backdrop logic in fetchPageCollections for hero rotator
old_hero_insert = """    if (!heroCollection.items.empty()) {
        collections.insert(collections.begin(), std::move(heroCollection));
    }"""

new_hero_insert = """    // Garantizar que ningún slide del Hero Banner se quede sin imagen de fondo
    std::string fallbackHeroBackdrop;
    for (const auto& h : heroCollection.items) {
        if (!h.backdropUrl.empty()) {
            fallbackHeroBackdrop = h.backdropUrl;
            break;
        }
    }
    if (fallbackHeroBackdrop.empty()) {
        for (const auto& col : collections) {
            for (const auto& item : col.items) {
                if (!item.backdropUrl.empty()) {
                    fallbackHeroBackdrop = item.backdropUrl;
                    break;
                }
            }
            if (!fallbackHeroBackdrop.empty()) break;
        }
    }
    if (!fallbackHeroBackdrop.empty()) {
        for (auto& h : heroCollection.items) {
            if (h.backdropUrl.empty()) {
                h.backdropUrl = fallbackHeroBackdrop;
            }
        }
    }

    if (!heroCollection.items.empty()) {
        collections.insert(collections.begin(), std::move(heroCollection));
    }"""

if old_hero_insert in content:
    content = content.replace(old_hero_insert, new_hero_insert, 1)
    print("hero fallback backdrop patched!")
else:
    print("hero_insert target not found!")

with open(path, "w", encoding="utf-8") as f:
    f.write(content)
print("luna_session.cpp updated!")
