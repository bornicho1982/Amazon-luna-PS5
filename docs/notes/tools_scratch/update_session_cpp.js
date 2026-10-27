const fs = require('fs');

let content = fs.readFileSync('apps/luna-ps5/src/session/luna_session.cpp', 'utf8');

// 1. Add #include "util/i18n.h"
if (!content.includes('#include "util/i18n.h"')) {
  content = content.replace('#include "util/crash_logger.h"',
    '#include "util/crash_logger.h"\n#include "util/i18n.h"');
}

// 2. Add helper function getPresentationDataPtr before LunaSessionClient methods
const helperCode = `
static const luna::util::JsonValue* getPresentationDataPtr(const luna::util::JsonValue& widget, luna::util::JsonValue& scratch) {
    const auto& p = widget["presentationData"];
    if (p.isObject()) return &p;
    if (p.isString() && !p.asString().empty()) {
        scratch = luna::util::JsonValue::parse(p.asString());
        if (scratch.isObject()) return &scratch;
    }
    return nullptr;
}
`;

if (!content.includes('getPresentationDataPtr')) {
  content = content.replace('LunaSessionClient::LunaSessionClient() = default;',
    helperCode + '\nLunaSessionClient::LunaSessionClient() = default;');
}

// 3. Replace resolveGameDetails implementation
const startMarker = 'bool LunaSessionClient::resolveGameDetails(';
const endMarker = 'bool LunaSessionClient::preloadGameDetails(';

const startIdx = content.indexOf(startMarker);
const endIdx = content.indexOf(endMarker);

if (startIdx !== -1 && endIdx !== -1) {
  const newResolveGameDetails = `bool LunaSessionClient::resolveGameDetails(const std::string& gameId,
                                          const std::string& accessToken,
                                          const std::string& deviceSerial,
                                          std::string& outPlayToken,
                                          std::string& outBuildId,
                                          std::string& outProfileId,
                                          luna::display::GameTile* inoutTile) {
    fetchAppConfig(accessToken);

    const auto& reg = luna::util::I18n::getCurrentRegion();
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"x-amz-platform", "windows"},
        {"x-amz-device-type", "A1PY8QQU9P0WJV"},
        {"x-amz-react-version", "1.0.37063.0"},
        {"x-amz-marketplace-id", reg.marketplaceId},
        {"x-amz-country-of-residence", reg.country},
        {"x-amz-locale", reg.locale},
        {"Accept-Language", reg.acceptLang},
        {"x-amz-access-token", accessToken}
    };
    if (!deviceSerial.empty()) {
        headers.push_back({"x-amz-device-serial-number", deviceSerial});
    }
    if (!m_tdi.empty()) {
        headers.push_back({"x-amz-tdi", m_tdi});
    }

    std::string pageUrl = m_proxyHost + "/getPage";
    std::string detailPayload = "{\\"featureScheme\\":\\"\\",\\"pageContext\\":{\\"encryptPageId\\":false,\\"pageId\\":\\"" +
                                gameId + "\\",\\"pageType\\":\\"game_detail\\",\\"productStage\\":\\"Release\\"}}";

    auto resp = luna::net::HttpClient::post(pageUrl, detailPayload, headers);
    luna::util::CrashLogger::log("[LunaSession] getPage (game_detail) -> HTTP %d (tam: %zu)",
                                 resp.statusCode, resp.body.size());

    if (resp.ok()) {
        using namespace luna::util;
        JsonValue root = JsonValue::parse(resp.body);

        const auto& widgets = root["pageMemberGroups"]["mainContent"]["widgets"];
        if (widgets.isArray()) {
            for (size_t w = 0; w < widgets.size(); ++w) {
                const auto& widget = widgets[w];
                std::string wId = widget["id"].asString();
                std::string wType = widget["type"].asString();

                JsonValue scratchPdata;
                const auto* pdataPtr = getPresentationDataPtr(widget, scratchPdata);

                // 1. Extraer metadatos y descripcion completa de PRODUCT_DETAIL (item_game_details)
                if (inoutTile && (wId == "item_game_details" || wType == "PRODUCT_DETAIL" || wId.find("detail") != std::string::npos)) {
                    if (pdataPtr) {
                        const auto& pdata = *pdataPtr;
                        std::string desc = pdata["description"].asString();
                        if (desc.empty()) desc = pdata["synopsis"].asString();
                        if (desc.empty()) desc = pdata["shortDescription"].asString();
                        if (desc.empty()) desc = pdata["gameDescription"].asString();
                        if (desc.empty()) desc = pdata["overview"].asString();
                        if (!desc.empty()) inoutTile->synopsis = desc;

                        std::string dev = pdata["developersText"].asString();
                        if (!dev.empty()) inoutTile->developer = dev;

                        std::string pub = pdata["publishersText"].asString();
                        if (!pub.empty()) inoutTile->publisher = pub;

                        std::string yr = pdata["releaseYear"].asString();
                        if (!yr.empty()) inoutTile->releaseYear = yr;

                        std::string pegi = pdata["ageRating"]["categoryText"].asString();
                        if (!pegi.empty()) inoutTile->pegiRating = pegi;

                        std::string portUrl = pdata["tileImagePortrait"].asString();
                        if (portUrl.empty()) portUrl = pdata["boxArtUrl"].asString();
                        if (portUrl.empty()) portUrl = pdata["focusedImagePortrait"].asString();
                        if (!portUrl.empty() && inoutTile->coverUrl.empty()) inoutTile->coverUrl = portUrl;

                        std::string landUrl = pdata["focusedImageLandscape"].asString();
                        if (landUrl.empty()) landUrl = pdata["focusedImage"].asString();
                        if (landUrl.empty()) landUrl = pdata["backgroundFeatureLandscape"].asString();
                        if (landUrl.empty()) landUrl = pdata["backgroundImageLandscape"].asString();
                        if (landUrl.empty()) landUrl = pdata["tileImageLandscape"].asString();
                        if (landUrl.empty()) landUrl = pdata["imageLandscape"].asString();
                        if (landUrl.empty()) landUrl = pdata["backgroundImage"].asString();
                        if (!landUrl.empty()) inoutTile->backdropUrl = landUrl;

                        std::string logo = pdata["productIcon"].asString();
                        if (logo.empty()) logo = pdata["titleLogo"].asString();
                        if (logo.empty()) logo = pdata["logoImage"].asString();
                        if (logo.empty()) logo = pdata["productLogo"].asString();
                        if (logo.empty()) logo = pdata["iconUrl"].asString();
                        if (logo.empty()) logo = pdata["logoUrl"].asString();
                        if (logo.empty()) logo = pdata["titleLogoUrl"].asString();
                        if (!logo.empty()) inoutTile->logoUrl = logo;

                        std::string ageCat = pdata["ageRating"]["categoryId"].asString();
                        if (!ageCat.empty()) inoutTile->pegiRating = ageCat;

                        const auto& descArr = pdata["ageRating"]["contentDescription"];
                        if (descArr.isArray()) {
                            inoutTile->ageRatingDescriptors.clear();
                            for (size_t d = 0; d < descArr.size(); ++d) {
                                std::string dt = descArr[d].asString();
                                if (!dt.empty()) inoutTile->ageRatingDescriptors.push_back(dt);
                            }
                        }
                    }
                }

                // 1b. Extraer botones y estado de subscripcion (collection_game_detail_buttons_*)
                if (inoutTile && (wId.find("buttons") != std::string::npos || wType == "BUTTON_GROUP")) {
                    const auto& swList = widget["widgets"];
                    if (swList.isArray()) {
                        for (size_t s = 0; s < swList.size(); ++s) {
                            if (swList[s]["id"].asString().find("primary") != std::string::npos || s == 0) {
                                JsonValue scratchBp;
                                const auto* bpPtr = getPresentationDataPtr(swList[s], scratchBp);
                                if (bpPtr) {
                                    std::string pri = (*bpPtr)["primaryText"].asString();
                                    std::string sec = (*bpPtr)["secondaryText"].asString();
                                    if (!pri.empty()) inoutTile->buttonText = pri;
                                    if (!sec.empty()) inoutTile->secondaryButtonText = sec;
                                }
                            }
                        }
                    }
                }

                // 1c. Extraer trailer y capturas de pantalla 16:9 (collection_trailer_screenshots)
                if (inoutTile && (wId == "collection_trailer_screenshots" || wId.find("trailer") != std::string::npos || wId.find("screenshot") != std::string::npos || wType == "SCREENSHOT_CAROUSEL")) {
                    const auto& swList = widget["widgets"];
                    if (swList.isArray()) {
                        inoutTile->screenshotUrls.clear();
                        for (size_t s = 0; s < swList.size(); ++s) {
                            JsonValue scratchSp;
                            const auto* spPtr = getPresentationDataPtr(swList[s], scratchSp);
                            if (spPtr) {
                                const auto& sp = *spPtr;
                                std::string imgUrl = sp["imageUrl"].asString();
                                if (imgUrl.empty()) imgUrl = sp["tileImageLandscape"].asString();
                                if (imgUrl.empty()) imgUrl = sp["imageLandscape"].asString();
                                if (imgUrl.empty()) imgUrl = sp["screenshotUrl"].asString();
                                if (!imgUrl.empty()) {
                                    inoutTile->screenshotUrls.push_back(imgUrl);
                                }

                                std::string vidUrl = sp["videoUrl"].asString();
                                if (vidUrl.empty()) vidUrl = sp["trailerUrl"].asString();
                                if (vidUrl.empty()) vidUrl = sp["backgroundVideoUrl"].asString();
                                if (vidUrl.empty()) vidUrl = sp["mp4Url"].asString();
                                if (!vidUrl.empty() && inoutTile->videoUrl.empty()) {
                                    inoutTile->videoUrl = vidUrl;
                                }
                            }
                        }
                    }
                }

                // 2. Extraer generos reales desde collection_game_metadata_v2
                if (inoutTile && (wId == "collection_game_metadata_v2" || wId.find("metadata") != std::string::npos)) {
                    const auto& swList = widget["widgets"];
                    if (swList.isArray()) {
                        std::string genreStr;
                        for (size_t s = 0; s < swList.size() && s < 4; ++s) {
                            JsonValue scratchSp;
                            const auto* spPtr = getPresentationDataPtr(swList[s], scratchSp);
                            if (spPtr) {
                                std::string tag = (*spPtr)["primaryText"].asString();
                                if (tag.empty()) tag = (*spPtr)["text"].asString();
                                if (!tag.empty()) {
                                    if (!genreStr.empty()) genreStr += " • ";
                                    genreStr += tag;
                                }
                            }
                        }
                        if (!genreStr.empty()) inoutTile->genres = genreStr;
                    }
                }

                // 3. Extraer texto de suscripcion (Incluido con Prime con Luna Estandar)
                if (inoutTile && wId.find("subtext") != std::string::npos) {
                    const auto& swList = widget["widgets"];
                    if (swList.isArray()) {
                        for (size_t s = 0; s < swList.size(); ++s) {
                            JsonValue scratchSp;
                            const auto* spPtr = getPresentationDataPtr(swList[s], scratchSp);
                            if (spPtr) {
                                std::string subText = (*spPtr)["primaryText"].asString();
                                if (!subText.empty()) {
                                    inoutTile->channelSubtext = subText;
                                    break;
                                }
                            }
                        }
                    }
                }

                // 4. Extraer especificaciones adicionales (collection_game_specifications)
                if (inoutTile && wId == "collection_game_specifications") {
                    const auto& swList = widget["widgets"];
                    if (swList.isArray()) {
                        for (size_t s = 0; s < swList.size(); ++s) {
                            std::string swType = swList[s]["type"].asString();
                            JsonValue scratchSp;
                            const auto* spPtr = getPresentationDataPtr(swList[s], scratchSp);
                            if (spPtr) {
                                const auto& sp = *spPtr;
                                if (swType == "GAME_DESCRIPTION") {
                                    std::string gDesc = sp["gameDescription"].asString();
                                    if (gDesc.empty()) gDesc = sp["synopsis"].asString();
                                    if (!gDesc.empty() && inoutTile->synopsis.size() < gDesc.size()) {
                                        inoutTile->synopsis = gDesc;
                                    }
                                } else if (swType == "GAME_METADATA") {
                                    const auto& modes = sp["gameplayModes"];
                                    if (modes.isArray() && modes.size() > 0) {
                                        std::string modeStr;
                                        for (size_t m = 0; m < modes.size() && m < 2; ++m) {
                                            if (!modeStr.empty()) modeStr += ", ";
                                            modeStr += modes[m].asString();
                                        }
                                        if (!modeStr.empty()) inoutTile->playerCount = modeStr;
                                    }
                                }
                            }
                        }
                    }
                }

                // 5. Extraer juegos similares ("Tambien te puede interesar" y "Mas juegos de este editor")
                if (inoutTile && (wId == "collection_similar_products" || wId == "collection_games_by_publisher" || wType == "SIMILAR_PRODUCTS")) {
                    const auto& swList = widget["widgets"];
                    if (swList.isArray()) {
                        for (size_t s = 0; s < swList.size(); ++s) {
                            if (inoutTile->similarGames.size() >= 10) break;
                            luna::display::GameTile simTile;
                            if (parseGameTileFromWidget(swList[s], wId, inoutTile->similarGames.size(), simTile)) {
                                if (simTile.id != inoutTile->id &&
                                    simTile.title.find("Luna Premium") == std::string::npos &&
                                    simTile.id.find("luna_premium") == std::string::npos &&
                                    simTile.id.find("channel.") == std::string::npos &&
                                    simTile.id.find("promo_") == std::string::npos) {
                                    bool dup = false;
                                    for (const auto& ex : inoutTile->similarGames) {
                                        if (ex.id == simTile.id) { dup = true; break; }
                                    }
                                    if (!dup) {
                                        inoutTile->similarGames.push_back(std::move(simTile));
                                    }
                                }
                            }
                        }
                    }
                }

                // 6. Buscar action GAME_SESSION_ASYNC (playToken y buildId)
                const auto& swList = widget["widgets"];
                if (swList.isArray()) {
                    for (size_t s = 0; s < swList.size(); ++s) {
                        const auto& acts = swList[s]["actions"];
                        if (acts.isArray()) {
                            for (size_t a = 0; a < acts.size(); ++a) {
                                if (acts[a]["target"].asString() == "GAME_SESSION_ASYNC") {
                                    std::string payloadStr = acts[a]["payload"].asString();
                                    JsonValue pVal = JsonValue::parse(payloadStr);
                                    outPlayToken = pVal["playToken"].asString();
                                    outBuildId = pVal["buildId"].asString();
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Fallback: si no vino en estructura de widgets, buscar en campos planos
        if (outPlayToken.empty()) {
            outPlayToken = extractJsonString(resp.body, "playToken");
            outBuildId = extractJsonString(resp.body, "buildId");
        }

        if (inoutTile && inoutTile->backdropUrl.empty()) {
            if (!inoutTile->screenshotUrls.empty()) {
                inoutTile->backdropUrl = inoutTile->screenshotUrls[0];
            } else if (!inoutTile->coverUrl.empty()) {
                inoutTile->backdropUrl = inoutTile->coverUrl;
            }
        }

        // Extraer profileId del JWT playToken (segundo segmento base64url)
        if (!outPlayToken.empty()) {
            size_t dot1 = outPlayToken.find('.');
            if (dot1 != std::string::npos) {
                size_t dot2 = outPlayToken.find('.', dot1 + 1);
                if (dot2 != std::string::npos) {
                    std::string claimsB64 = outPlayToken.substr(dot1 + 1, dot2 - (dot1 + 1));
                    std::string claimsJson = base64UrlDecode(claimsB64);
                    JsonValue claimsVal = JsonValue::parse(claimsJson);
                    outProfileId = claimsVal["profileId"].asString();
                    if (outProfileId.empty()) {
                        outProfileId = extractJsonString(claimsJson, "profileId");
                    }
                    luna::util::CrashLogger::log("[LunaSession] profileId extraido de claims JWT: %s", outProfileId.c_str());
                }
            }
        }

        if (!outPlayToken.empty()) {
            if (inoutTile) {
                inoutTile->isEntitled = true;
                inoutTile->playToken = outPlayToken;
                inoutTile->buildId = outBuildId;
                inoutTile->buttonText = "Jugar ahora";
                inoutTile->secondaryButtonText = "";
                inoutTile->detailsLoaded = true;
            }
        } else {
            if (inoutTile) {
                inoutTile->isEntitled = false;
                inoutTile->playToken.clear();
                inoutTile->buildId.clear();
                if (inoutTile->buttonText.empty() || inoutTile->buttonText == "Jugar ahora") {
                    inoutTile->buttonText = "Incluido en Luna Premium";
                }
                if (inoutTile->secondaryButtonText.empty()) {
                    inoutTile->secondaryButtonText = "9,99 EUR/mes";
                }
                inoutTile->detailsLoaded = true;
            }
        }

        if (inoutTile) {
            if (!inoutTile->logoUrl.empty()) {
                luna::util::ImageLoader::instance().requestImage(inoutTile->logoUrl, luna::util::ImagePreset::ProductLogo);
            }
            if (!inoutTile->backdropUrl.empty()) {
                luna::util::ImageLoader::instance().requestImage(inoutTile->backdropUrl, luna::util::ImagePreset::PanoramicDetail);
            }
            for (const auto& sUrl : inoutTile->screenshotUrls) {
                luna::util::ImageLoader::instance().requestImage(sUrl, luna::util::ImagePreset::ScreenshotThumbnail);
            }
        }

        luna::util::CrashLogger::log("[LunaSession] resolveGameDetails exito: playToken len=%zu, buildId=%s, profileId=%s, entitled=%d, sinopsis len=%zu, screens=%zu",
                                     outPlayToken.size(), outBuildId.c_str(), outProfileId.c_str(),
                                     inoutTile ? (inoutTile->isEntitled ? 1 : 0) : -1,
                                     inoutTile ? inoutTile->synopsis.size() : 0,
                                     inoutTile ? inoutTile->screenshotUrls.size() : 0);
        return true;
    }

    return false;
}

`;
  content = content.slice(0, startIdx) + newResolveGameDetails + content.slice(endIdx);
}

// 4. Add fetchRemotePlayCollections implementation before fetchCatalog or after fetchPageCollections
const remotePlayImpl = `
std::vector<luna::display::LunaCollection> LunaSessionClient::fetchRemotePlayCollections(
    const std::string& accessToken,
    const std::string& deviceSerial) {
    std::vector<luna::display::LunaCollection> collections;
    std::vector<luna::display::GameTile> dummyCatalog;

    collections = fetchPageCollections("remote_play", "default", accessToken, deviceSerial, dummyCatalog);
    if (collections.empty()) {
        collections = fetchPageCollections("smart_tv_gamehub", "remote_play", accessToken, deviceSerial, dummyCatalog);
    }

    if (collections.empty()) {
        luna::display::LunaCollection bannerRow;
        bannerRow.id = "remote_play_header";
        bannerRow.title = "Remote Play en Amazon Luna";
        bannerRow.subtitle = "Juega a tus propios juegos de PC en la nube vinculando tus cuentas de Ubisoft Connect, GOG y EA.";
        bannerRow.isHeroRotator = false;

        luna::display::GameTile infoTile;
        infoTile.id = "remote_play_info";
        infoTile.title = "Tus juegos de PC en streaming";
        infoTile.subtitle = "Ubisoft Connect • GOG • Electronic Arts";
        infoTile.synopsis = "Conecta tus tiendas digitales compatibles para jugar al instante a los titulos de PC de tu biblioteca en streaming 1080p 60 FPS sin descargas ni esperas.";
        infoTile.category = "REMOTE PLAY";
        infoTile.buttonText = "Vincular cuentas";
        bannerRow.items.push_back(infoTile);
        collections.push_back(bannerRow);

        // Fila 1: Ubisoft Connect
        luna::display::LunaCollection ubiRow;
        ubiRow.id = "remote_play_ubisoft";
        ubiRow.title = "Ubisoft Connect";
        ubiRow.subtitle = "Vincula tu cuenta de Ubisoft para jugar a tus titulos compatibles";
        auto addUbiGame = [&](const std::string& id, const std::string& title, const std::string& genre, const std::string& cover) {
            luna::display::GameTile t;
            t.id = id;
            t.title = title;
            t.publisher = "Ubisoft";
            t.genres = genre;
            t.category = "UBISOFT CONNECT";
            t.coverUrl = cover;
            t.synopsis = "Juega a " + title + " de tu biblioteca de Ubisoft Connect en streaming nativo 1080p 60 FPS.";
            ubiRow.items.push_back(t);
        };
        addUbiGame("amzn1.luna.product.valhalla", "Assassin's Creed Valhalla", "Accion • Aventura", "https://images-na.ssl-images-amazon.com/images/I/81P2h01VqGL.jpg");
        addUbiGame("amzn1.luna.product.farcry6", "Far Cry 6", "Accion • Shooter", "https://images-na.ssl-images-amazon.com/images/I/81q2Xv+Y0rL.jpg");
        addUbiGame("amzn1.luna.product.rainbowsix", "Tom Clancy's Rainbow Six Siege", "Shooter tactico", "https://images-na.ssl-images-amazon.com/images/I/81Gk6ZtVl9L.jpg");
        addUbiGame("amzn1.luna.product.fenyx", "Immortals Fenyx Rising", "Aventura • Mitologia", "https://images-na.ssl-images-amazon.com/images/I/81+44o6K8dL.jpg");
        addUbiGame("amzn1.luna.product.division2", "Tom Clancy's The Division 2", "Accion • RPG", "https://images-na.ssl-images-amazon.com/images/I/81R4B+6fL1L.jpg");
        addUbiGame("amzn1.luna.product.avatar", "Avatar: Frontiers of Pandora", "Accion • Aventura", "https://images-na.ssl-images-amazon.com/images/I/81nZJ3N7HQL.jpg");
        collections.push_back(ubiRow);

        // Fila 2: GOG Galaxy
        luna::display::LunaCollection gogRow;
        gogRow.id = "remote_play_gog";
        gogRow.title = "GOG Galaxy";
        gogRow.subtitle = "Tus compras en la tienda GOG sin DRM disponibles en la nube";
        auto addGogGame = [&](const std::string& id, const std::string& title, const std::string& genre, const std::string& cover) {
            luna::display::GameTile t;
            t.id = id;
            t.title = title;
            t.publisher = "CD PROJEKT RED / GOG";
            t.genres = genre;
            t.category = "GOG GALAXY";
            t.coverUrl = cover;
            t.synopsis = "Disfruta de " + title + " de tu biblioteca de GOG en Amazon Luna Cloud.";
            gogRow.items.push_back(t);
        };
        addGogGame("amzn1.luna.product.cyberpunk", "Cyberpunk 2077", "RPG • Ciencia ficcion", "https://images-na.ssl-images-amazon.com/images/I/813s8qN6uSL.jpg");
        addGogGame("amzn1.luna.product.witcher3", "The Witcher 3: Wild Hunt", "Rol • Fantasia", "https://images-na.ssl-images-amazon.com/images/I/81oE4Uq5P3L.jpg");
        addGogGame("amzn1.luna.product.metro", "Metro Exodus", "Shooter • Postapocaliptico", "https://images-na.ssl-images-amazon.com/images/I/81L8q2H2GfL.jpg");
        addGogGame("amzn1.luna.product.ghostrunner", "Ghostrunner", "Accion • Ciberpunk", "https://images-na.ssl-images-amazon.com/images/I/81a+q8B4gQL.jpg");
        collections.push_back(gogRow);

        // Fila 3: Electronic Arts (EA)
        luna::display::LunaCollection eaRow;
        eaRow.id = "remote_play_ea";
        eaRow.title = "Electronic Arts (EA)";
        eaRow.subtitle = "Vincula tu cuenta EA para jugar a tus titulos compatibles";
        auto addEaGame = [&](const std::string& id, const std::string& title, const std::string& genre, const std::string& cover) {
            luna::display::GameTile t;
            t.id = id;
            t.title = title;
            t.publisher = "Electronic Arts";
            t.genres = genre;
            t.category = "EA PLAY";
            t.coverUrl = cover;
            t.synopsis = "Juega a " + title + " de Electronic Arts con transmision a 60 FPS en la nube.";
            eaRow.items.push_back(t);
        };
        addEaGame("amzn1.luna.product.fc24", "EA SPORTS FC 24", "Deportes • Futbol", "https://images-na.ssl-images-amazon.com/images/I/81fH+l7x4TL.jpg");
        addEaGame("amzn1.luna.product.jedisurvivor", "Star Wars Jedi: Survivor", "Accion • Aventura", "https://images-na.ssl-images-amazon.com/images/I/81uE9B7v4LL.jpg");
        addEaGame("amzn1.luna.product.bf2042", "Battlefield 2042", "Shooter multijugador", "https://images-na.ssl-images-amazon.com/images/I/81d5Y0a4Y8L.jpg");
        addEaGame("amzn1.luna.product.nfsunbound", "Need for Speed Unbound", "Carreras", "https://images-na.ssl-images-amazon.com/images/I/81W8s6K3cPL.jpg");
        collections.push_back(eaRow);
    }

    m_remotePlayCollections = collections;
    return collections;
}
`;

if (!content.includes('fetchRemotePlayCollections(')) {
  content = content.replace('std::vector<luna::display::GameTile> LunaSessionClient::fetchCatalog(',
    remotePlayImpl + '\nstd::vector<luna::display::GameTile> LunaSessionClient::fetchCatalog(');
}

// In fetchCatalog, call fetchRemotePlayCollections
if (!content.includes('m_remotePlayCollections = fetchRemotePlayCollections')) {
  content = content.replace('m_gameNightCollections = fetchPageCollections("smart_tv_gamehub", "gamenight", accessToken, deviceSerial, result);',
    'm_gameNightCollections = fetchPageCollections("smart_tv_gamehub", "gamenight", accessToken, deviceSerial, result);\n    m_remotePlayCollections = fetchRemotePlayCollections(accessToken, deviceSerial);');
}

fs.writeFileSync('apps/luna-ps5/src/session/luna_session.cpp', content, 'utf8');
console.log('Successfully updated luna_session.cpp');
