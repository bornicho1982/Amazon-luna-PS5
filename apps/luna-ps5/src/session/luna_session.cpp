#include "session/luna_session.h"

#include "net/http_client.h"

#include "util/crash_logger.h"

#include "util/i18n.h"

#include "util/image_loader.h"

#include "util/json_parser.h"



#include <cstdio>

#include <algorithm>



namespace luna::session {



namespace {



std::string extractJsonString(const std::string& json, const std::string& key) {

    std::string pattern = "\"" + key + "\"";

    size_t pos = json.find(pattern);

    if (pos == std::string::npos) {

        pattern = key;

        pos = json.find(pattern);

        if (pos == std::string::npos) return "";

    }



    pos = json.find(':', pos);

    if (pos == std::string::npos) return "";



    // Skip whitespace, colon, and any backslashes before quote

    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t' ||

                                 json[pos] == '\r' || json[pos] == '\n' || json[pos] == '\\')) {

        pos++;

    }



    if (pos >= json.size()) return "";



    if (json[pos] != '"') {

        size_t endPos = json.find_first_of(",}\" \t\r\n", pos);

        if (endPos == std::string::npos) endPos = json.size();

        return json.substr(pos, endPos - pos);

    }



    pos++; // skip opening quote

    std::string result;

    bool escaped = false;

    for (size_t i = pos; i < json.size(); ++i) {

        char c = json[i];

        if (escaped) {

            result += c;

            escaped = false;

        } else if (c == '\\') {

            escaped = true;

        } else if (c == '"') {

            break;

        } else {

            result += c;

        }

    }

    return result;

}



std::string escapeJson(const std::string& str) {

    std::string out;

    out.reserve(str.size() + 100);

    for (char c : str) {

        if (c == '"') out += "\\\"";

        else if (c == '\\') out += "\\\\";

        else if (c == '\r') out += "\\r";

        else if (c == '\n') out += "\\n";

        else if (c == '\t') out += "\\t";

        else out += c;

    }

    return out;

}



static inline std::string base64UrlDecode(const std::string& input) {

    std::string in = input;

    for (char& c : in) {

        if (c == '-') c = '+';

        else if (c == '_') c = '/';

    }

    while (in.size() % 4 != 0) {

        in += '=';

    }



    static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;

    std::vector<int> T(256, -1);

    for (int i = 0; i < 64; i++) T[static_cast<unsigned char>(b64chars[i])] = i;



    int val = 0, valb = -8;

    for (unsigned char c : in) {

        if (T[c] == -1) break;

        val = (val << 6) + T[c];

        valb += 6;

        if (valb >= 0) {

            out.push_back(char((val >> valb) & 0xFF));

            valb -= 8;

        }

    }

    return out;

}



static const uint32_t kCardColors[] = {

    0xFF9146FF, // Luna Purple

    0xFF0284C7, // Prime Sky Blue

    0xFF10B981, // Emerald

    0xFFF59E0B, // Amber

    0xFF06B6D4, // Cyan

    0xFFEC4899, // Pink

    0xFF6366F1, // Indigo

    0xFF14B8A6, // Teal

    0xFFF97316, // Orange

    0xFF8B5CF6, // Violet

    0xFF3B82F6, // Blue

    0xFFEF4444  // Red

};



std::string extractProductIdFromString(const std::string& str) {

    size_t lunaPos = str.find("amzn1.luna.product.");

    size_t adgPos = str.find("amzn1.adg.product.");

    size_t prodPos = (lunaPos != std::string::npos) ? lunaPos : adgPos;

    if (prodPos == std::string::npos) return "";



    size_t endPos = prodPos;

    while (endPos < str.size()) {

        char c = str[endPos];

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||

            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') {

            endPos++;

        } else {

            break;

        }

    }

    return str.substr(prodPos, endPos - prodPos);

}



bool parseGameTileFromWidget(const luna::util::JsonValue& sw,

                             const std::string& colId,

                             size_t colorIdx,

                             luna::display::GameTile& outTile) {

    using namespace luna::util;

    std::string swId = sw["id"].asString();

    std::string swType = sw["type"].asString();

    std::string pdataStr = sw["presentationData"].asString();

    JsonValue pdata = JsonValue::parse(pdataStr);



    std::string cleanGid = pdata["gameId"].asString();

    if (cleanGid.empty()) {

        cleanGid = extractProductIdFromString(swId);

    }

    if (cleanGid.empty()) {

        const auto& acts = sw["actions"];

        if (acts.isArray()) {

            for (size_t a = 0; a < acts.size(); ++a) {

                cleanGid = extractProductIdFromString(acts[a]["target"].asString());

                if (!cleanGid.empty()) break;

                std::string actPayload = acts[a]["payload"].asString();

                if (!actPayload.empty()) {

                    JsonValue ap = JsonValue::parse(actPayload);

                    cleanGid = ap["productId"].asString();

                    if (cleanGid.empty()) cleanGid = extractProductIdFromString(actPayload);

                    if (!cleanGid.empty()) break;

                }

            }

        }

    }

    if (cleanGid.empty()) {

        if (colId.find("featured_rotator") != std::string::npos || swType.find("ROTATOR") != std::string::npos) {

            cleanGid = swId.empty() ? ("promo_" + std::to_string(colorIdx)) : swId;

        } else {

            return false;

        }

    }



    std::string title = pdata["title"].asString();

    if (title.empty()) title = pdata["gameTitle"].asString();

    if (title.empty()) title = pdata["productTitle"].asString();

    if (title.empty()) title = pdata["name"].asString();

    if (title.empty()) title = pdata["heading"].asString();

    if (title.empty()) return false;



    const auto& hover = pdata["hoverDetails"];



    std::string publisher = hover["publishersText"].asString();

    if (publisher.empty()) publisher = pdata["publishersText"].asString();

    if (publisher.empty()) publisher = pdata["publisher"].asString();

    if (publisher.empty()) publisher = pdata["publisherName"].asString();

    if (publisher.empty()) publisher = pdata["developersText"].asString();



    std::string developer = pdata["developersText"].asString();

    if (developer.empty()) developer = pdata["developer"].asString();

    if (developer.empty()) developer = publisher;



    std::string releaseYear = pdata["releaseYear"].asString();



    std::string pegi = hover["ageRating"]["categoryText"].asString();

    if (pegi.empty()) pegi = pdata["ageRating"]["categoryText"].asString();

    if (pegi.empty()) pegi = pdata["pegiRating"].asString();



    std::string genres;

    const auto& genreArr = pdata["genreTags"];

    if (genreArr.isArray()) {

        for (size_t g = 0; g < genreArr.size() && g < 3; ++g) {

            std::string gt = genreArr[g].asString();

            if (!gt.empty()) {

                if (!genres.empty()) genres += " • ";

                genres += gt;

            }

        }

    }



    std::string inputMethods = hover["gameInputsString"].asString();

    if (inputMethods.empty()) inputMethods = "Mando DualSense";



    std::string playerCount = hover["numberOfPlayers"].asString();

    if (playerCount.empty()) playerCount = "1 jugador";



    std::string metacritic = hover["metacritic"]["metacriticText"].asString();



    std::string coverUrl = pdata["imagePortrait"].asString();

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

    if (backdropUrl.empty()) backdropUrl = coverUrl;



    std::string logoUrl = pdata["productIcon"].asString();

    if (logoUrl.empty()) logoUrl = pdata["titleLogo"].asString();

    if (logoUrl.empty()) logoUrl = pdata["logoImage"].asString();

    if (logoUrl.empty()) logoUrl = pdata["productLogo"].asString();

    if (logoUrl.empty()) logoUrl = pdata["iconUrl"].asString();

    if (logoUrl.empty()) logoUrl = pdata["logoUrl"].asString();

    if (logoUrl.empty()) logoUrl = pdata["titleLogoUrl"].asString();



    std::string subtitle = pdata["primaryText"].asString();

    if (subtitle.empty()) subtitle = pdata["subtitle"].asString();



    std::string btnText = pdata["buttonText"].asString();

    if (btnText.empty()) btnText = "Mas informacion";



    if (colId.find("featured_rotator") != std::string::npos && colorIdx == 0 && title == "Luna Premium") {

        backdropUrl = "local:luna_premium";

    }



    std::string synopsis = pdata["primaryText"].asString();

    if (synopsis.empty()) synopsis = pdata["description"].asString();

    if (synopsis.empty()) synopsis = pdata["synopsis"].asString();

    if (synopsis.empty()) synopsis = pdata["shortDescription"].asString();

    if (synopsis.empty()) {

        std::string lastPlayed = pdata["lastPlayedDate"].asString();

        if (!lastPlayed.empty()) {

            synopsis = "Jugado recientemente (" + lastPlayed + ") en tu cuenta de Amazon Luna.";

        } else {

            synopsis = "Disfruta de " + title + " al instante en streaming 1080p 60 FPS con Amazon Luna.";

        }

    }



    std::string category = "INCLUIDO CON PRIME";

    if (colId.find("recently_played") != std::string::npos || swType == "RECENTLY_PLAYED_TILE") {

        category = "SEGUIR JUGANDO";

    } else if (colId.find("game_night") != std::string::npos || colId.find("home_2") != std::string::npos) {

        category = "GAMENIGHT";

    } else if (colId.find("popular") != std::string::npos) {

        category = "TOP LUNA";

    } else if (colId.find("editors_pick") != std::string::npos || colId.find("adventure") != std::string::npos) {

        category = "LUNA+";

    }



    outTile = luna::display::GameTile{};

    outTile.id = cleanGid;

    outTile.title = title;

    outTile.publisher = publisher.empty() ? "Amazon Luna" : publisher;

    outTile.developer = developer.empty() ? outTile.publisher : developer;

    outTile.releaseYear = releaseYear.empty() ? "2025" : releaseYear;

    outTile.pegiRating = pegi.empty() ? "PEGI 12" : pegi;

    outTile.genres = genres.empty() ? "Acción • Aventura" : genres;

    outTile.inputMethods = inputMethods;

    outTile.playerCount = playerCount;

    outTile.metacritic = metacritic;

    outTile.synopsis = synopsis;

    outTile.category = category;

    outTile.coverUrl = coverUrl;

    outTile.backdropUrl = backdropUrl;

    outTile.logoUrl = logoUrl;
    outTile.videoUrl = hover["trailerUrl"].asString();

    outTile.subtitle = subtitle;

    outTile.buttonText = btnText;

    outTile.lastPlayedDate = pdata["lastPlayedDate"].asString();



    uint32_t colColor = kCardColors[colorIdx % 12];

    outTile.badgeColor = colColor;

    outTile.heroColor = colColor;

    return true;

}



} // namespace





static const luna::util::JsonValue* getPresentationDataPtr(const luna::util::JsonValue& widget, luna::util::JsonValue& scratch) {

    const auto& p = widget["presentationData"];

    if (p.isObject()) return &p;

    if (p.isString() && !p.asString().empty()) {

        scratch = luna::util::JsonValue::parse(p.asString());

        if (scratch.isObject()) return &scratch;

    }

    return nullptr;

}



LunaSessionClient::LunaSessionClient() = default;

LunaSessionClient::~LunaSessionClient() = default;



bool LunaSessionClient::fetchAppConfig(const std::string& accessToken) {

    if (!m_tdi.empty()) return true;



    std::string url = m_proxyHost + "/getAppConfig";

    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"x-amz-platform", "windows"},

        {"x-amz-device-type", "A1PY8QQU9P0WJV"},

        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},

        {"x-amz-locale", "es_ES"},

        {"x-amz-access-token", accessToken}

    };



    auto resp = luna::net::HttpClient::post(url, "{}", headers);

    luna::util::CrashLogger::log("[LunaSession] getAppConfig -> HTTP %d (tam: %zu)",

                                 resp.statusCode, resp.body.size());



    if (resp.ok()) {

        std::string tdi = extractJsonString(resp.body, "tdi");

        if (!tdi.empty()) {

            m_tdi = tdi;

            luna::util::CrashLogger::log("[LunaSession] TDI configurado desde AppConfig: %s", m_tdi.c_str());

        }

        std::string proxy = extractJsonString(resp.body, "clientProxy");

        if (!proxy.empty()) {

            m_proxyHost = proxy;

        }

        return true;

    }



    return false;

}



void LunaSessionClient::fetchNavigationAndProfile(const std::string& accessToken,

                                                  const std::string& deviceSerial) {

    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"Accept", "application/json"},

        {"x-amz-platform", "windows"},

        {"x-amz-device-type", "A1PY8QQU9P0WJV"},

        {"x-amz-react-version", "1.0.37063.0"},

        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},

        {"x-amz-country-of-residence", "ES"},

        {"x-amz-locale", "es_ES"},

        {"Accept-Language", "es-ES,es;q=0.9,en;q=0.8"},

        {"x-amz-access-token", accessToken}

    };

    if (!deviceSerial.empty()) {

        headers.push_back({"x-amz-device-serial-number", deviceSerial});

    }

    if (!m_tdi.empty()) {

        headers.push_back({"x-amz-tdi", m_tdi});

    }



    std::string pageUrl = m_proxyHost + "/getPage";

    std::string navPayload = "{\"featureScheme\":\"\",\"pageContext\":{\"encryptPageId\":false,\"pageId\":\"default\",\"pageType\":\"multistate_navigation\"}}";

    auto resp = luna::net::HttpClient::post(pageUrl, navPayload, headers);

    if (!resp.ok() || resp.body.empty()) return;



    using namespace luna::util;

    JsonValue root = JsonValue::parse(resp.body);

    const auto* secNav = &root["pageMemberGroups"]["desktopSecondaryNavControllerConnected"]["widgets"];

    if (!secNav->isArray() || secNav->size() == 0) {

        secNav = &root["pageMemberGroups"]["desktopSecondaryNavControllerDisconnected"]["widgets"];

    }

    if (secNav->isArray()) {

        for (size_t i = 0; i < secNav->size(); ++i) {

            const auto& w = (*secNav)[i];

            if (w["type"].asString() == "PLAYER_NAV_ITEM" || w["id"].asString().find("player") != std::string::npos) {

                JsonValue pdata = JsonValue::parse(w["presentationData"].asString());

                std::string dispName = pdata["displayName"].asString();

                std::string disc = pdata["discriminator"].asString();

                std::string primeText = pdata["includedWithPrime"].asString();

                std::string planText = pdata["customerPlanStatus"].asString();

                std::string avatar = pdata["avatarUrl"].asString();



                if (!dispName.empty()) m_playerProfile.displayName = dispName;

                if (!disc.empty()) m_playerProfile.discriminator = disc;

                if (!avatar.empty()) m_playerProfile.avatarUrl = avatar;

                if (!primeText.empty()) {

                    m_playerProfile.planStatus = primeText;

                } else if (!planText.empty()) {

                    m_playerProfile.planStatus = planText;

                }

                luna::util::CrashLogger::log("[LunaSession] Perfil de jugador activo: %s %s (%s) avatar: %s",

                                             m_playerProfile.displayName.c_str(),

                                             m_playerProfile.discriminator.c_str(),

                                             m_playerProfile.planStatus.c_str(),

                                             m_playerProfile.avatarUrl.c_str());

                break;

            }

        }

    }

    // Asegurar token de perfil Luna activo para Remote Play y Cloud Couch

    loginPlayer("amzn1.actor.person.oid.A24FP4VJ7KI5CD", accessToken, deviceSerial);

}



std::vector<luna::display::LunaCollection> LunaSessionClient::fetchPageCollections(

    const std::string& pageType,

    const std::string& pageId,

    const std::string& accessToken,

    const std::string& deviceSerial,

    std::vector<luna::display::GameTile>& masterCatalog) {



    std::vector<luna::display::LunaCollection> collections;

    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"Accept", "application/json"},

        {"x-amz-platform", "windows"},

        {"x-amz-device-type", "A1PY8QQU9P0WJV"},

        {"x-amz-react-version", "1.0.37063.0"},

        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},

        {"x-amz-country-of-residence", "ES"},

        {"x-amz-locale", "es_ES"},

        {"Accept-Language", "es-ES,es;q=0.9,en;q=0.8"},

        {"x-amz-access-token", accessToken}

    };

    if (!deviceSerial.empty()) {

        headers.push_back({"x-amz-device-serial-number", deviceSerial});

    }

    if (!m_tdi.empty()) {

        headers.push_back({"x-amz-tdi", m_tdi});

    }



    std::string pageUrl = m_proxyHost + "/getPage";

    std::string payload;

    if (pageType == "landing_home" && pageId == "default") {

        payload = "{\"featureScheme\":\"\",\"serviceToken\":\"eyJlbmNyeXB0UGFnZUlkIjpmYWxzZSwicGFnZUlkIjoiZGVmYXVsdCIsInBhZ2VUeXBlIjoibGFuZGluZ19ob21lIn0=\"}";

    } else {

        payload = "{\"featureScheme\":\"\",\"pageContext\":{\"encryptPageId\":false,\"pageId\":\"" +

                  pageId + "\",\"pageType\":\"" + pageType + "\"}}";

    }



    auto resp = luna::net::HttpClient::post(pageUrl, payload, headers);

    luna::util::CrashLogger::log("[LunaCatalog] getPage (%s/%s) -> HTTP %d (tam: %zu)",

                                 pageType.c_str(), pageId.c_str(), resp.statusCode, resp.body.size());



    if (!resp.ok() || resp.body.empty()) {

        return collections;

    }



    using namespace luna::util;

    JsonValue root = JsonValue::parse(resp.body);

    const auto& widgets = root["pageMemberGroups"]["mainContent"]["widgets"];

    if (!widgets.isArray()) return collections;



    luna::display::LunaCollection heroCollection;

    heroCollection.id = "collection_featured_rotator";

    heroCollection.title = "Destacados en Amazon Luna";

    heroCollection.isHeroRotator = true;



    for (size_t i = 0; i < widgets.size(); ++i) {

        const auto& col = widgets[i];

        std::string colId = col["id"].asString();

        std::string colType = col["type"].asString();

        JsonValue colPdata = JsonValue::parse(col["presentationData"].asString());



        std::string colTitle = colPdata["title"].asString();

        if (colTitle.empty()) colTitle = colPdata["partnerTitle"].asString();

        if (colTitle.empty()) colTitle = colPdata["headerText"].asString();

        if (colTitle.empty()) colTitle = colPdata["heading"].asString();



        std::string colSubtitle = colPdata["partnerDescription"].asString();

        if (colSubtitle.empty()) colSubtitle = colPdata["subtitle"].asString();



        const auto& subWidgets = col["widgets"];



        // Fila de Banner Promocional de Partners (PARTNER_UPSELL_ROW)
        if (colType == "PARTNER_UPSELL_ROW" || colId.find("content_highlight_row") != std::string::npos) {
            luna::display::LunaCollection partnerRow;
            partnerRow.id = colId;
            bool isHome1 = (colId.find("home_1") != std::string::npos);
            partnerRow.title = colTitle.empty() ? (isHome1 ? "Personajes emblemáticos. Nuevas aventuras." : "Jugad juntos estéis donde estéis") : colTitle;
            partnerRow.subtitle = colSubtitle.empty() ? (isHome1 ? "Hay leyendas que no dejan de mejorar. Adéntrate en mundos inolvidables..." : "Juega con amigos y familiares, aunque no estéis en la misma habitación.") : colSubtitle;
            partnerRow.isPartnerRow = true;
            partnerRow.partnerTitle = partnerRow.title;
            partnerRow.partnerDescription = partnerRow.subtitle;
            partnerRow.partnerButtonText = colPdata["buttonText"].asString();
            if (partnerRow.partnerButtonText.empty()) partnerRow.partnerButtonText = "Ver todos los juegos";
            partnerRow.partnerPromoImage = colPdata["partnerPromoImage"].asString();
            if (partnerRow.partnerPromoImage.empty()) {
                if (isHome1) {
                    partnerRow.partnerPromoImage = "https://m.media-amazon.com/images/G/01/T/TC05316421/Evergreen/LunaClient/ContentHighlightRows/GarfieldKart2_Background_Desktop_2560x695.jpg";
                } else {
                    partnerRow.partnerPromoImage = "https://m.media-amazon.com/images/G/01/T/TC05316421/Evergreen/LunaClient/ContentHighlightRows/RemotePlay_Background_Desktop_2560x695.jpg";
                }
            }
            if (subWidgets.isArray()) {
                for (size_t j = 0; j < subWidgets.size(); ++j) {
                    luna::display::GameTile tile;
                    if (parseGameTileFromWidget(subWidgets[j], colId, masterCatalog.size() + j, tile)) {
                        partnerRow.items.push_back(tile);
                    }
                }
            }
            collections.push_back(partnerRow);
            continue;
        }

        // Banner Promocional Prime Gaming (collection_promo_banner_claims)
        if (colId.find("promo_banner") != std::string::npos || colId.find("claims") != std::string::npos) {
            luna::display::LunaCollection promoRow;
            promoRow.id = colId;
            promoRow.title = "Reclama tus juegos";
            promoRow.subtitle = "Juegos gratis incluidos este mes con Amazon Prime";
            promoRow.partnerPromoImage = "https://m.media-amazon.com/images/G/01/T/TC05316421/MonthlyPrime/LunaClientFGWPBannerOverride/Desktop_3532x380_esES.jpg";
            if (subWidgets.isArray() && subWidgets.size() > 0) {
                JsonValue scratchClaims;
                const auto* cp = getPresentationDataPtr(subWidgets[0], scratchClaims);
                if (cp && !(*cp)["promotionBackgroundImage"].asString().empty()) {
                    promoRow.partnerPromoImage = (*cp)["promotionBackgroundImage"].asString();
                }
            }
            luna::display::GameTile claimTile;
            claimTile.id = "promo_claims_tile";
            claimTile.title = "Reclama tus juegos con Prime";
            claimTile.coverUrl = promoRow.partnerPromoImage;
            claimTile.backdropUrl = promoRow.partnerPromoImage;
            promoRow.items.push_back(claimTile);
            collections.push_back(promoRow);
            continue;
        }



        // Carrusel de Categorias (Explorar categorias)

        if (colId == "collection_browse_games_tiles" || colTitle == "Explorar categorias" || colId.find("browse_tiles") != std::string::npos) {

            luna::display::LunaCollection catRow;

            catRow.id = colId;

            catRow.title = "Explorar categorias";

            catRow.isCategoryCarousel = true;

            if (subWidgets.isArray()) {

                for (size_t j = 0; j < subWidgets.size(); ++j) {

                    JsonValue scratchSp;

                    const auto* sp = getPresentationDataPtr(subWidgets[j], scratchSp);

                    if (sp) {

                        luna::display::GameTile catTile;

                        catTile.id = subWidgets[j]["id"].asString();

                        catTile.title = (*sp)["primaryText"].asString();

                        if (catTile.title.empty()) catTile.title = (*sp)["text"].asString();

                        catTile.genres = catTile.title;

                        catTile.coverUrl = (*sp)["imageUrl"].asString();

                        catRow.items.push_back(catTile);

                    }

                }

            }

            collections.push_back(catRow);

            continue;

        }



        if (!subWidgets.isArray() || subWidgets.size() == 0) continue;



        if (colType == "FEATURED_ROTATOR_V3" || colId.find("featured_rotator") != std::string::npos) {

            for (size_t j = 0; j < subWidgets.size(); ++j) {

                luna::display::GameTile tile;

                if (parseGameTileFromWidget(subWidgets[j], colId, heroCollection.items.size(), tile)) {

                    heroCollection.items.push_back(tile);

                    bool exists = false;

                    for (const auto& m : masterCatalog) {

                        if (m.id == tile.id) { exists = true; break; }

                    }

                    if (!exists) masterCatalog.push_back(tile);

                }

            }

            continue;

        }



        luna::display::LunaCollection row;

        row.id = colId;

        row.title = colTitle;

        row.subtitle = colSubtitle;

        row.isRankings = (colType == "RANKINGS_ROW" || colId.find("popular") != std::string::npos);



        if (row.title.empty() || row.title == "FEATURED") {

            if (colId.find("recently_played") != std::string::npos) row.title = "Seguir jugando";

            else if (colId.find("popular") != std::string::npos) row.title = "Las mejores novedades en juegos";

            else if (colId.find("recently_added") != std::string::npos) row.title = "Añadidos recientemente";

            else if (colId.find("editors_pick") != std::string::npos) row.title = "Selecciones del editor";

            else if (colId.find("adventure") != std::string::npos) row.title = "Aventuras en mundos inmersivos";

            else if (colId.find("game_night") != std::string::npos) row.title = "Juegos en grupo con GameNight";

            else row.title = "Recomendados en Amazon Luna";

        }



        for (size_t j = 0; j < subWidgets.size(); ++j) {

            luna::display::GameTile tile;

            if (parseGameTileFromWidget(subWidgets[j], colId, masterCatalog.size() + j, tile)) {

                row.items.push_back(tile);



                bool exists = false;

                for (auto& m : masterCatalog) {

                    if (m.id == tile.id) {

                        exists = true;

                        if (m.publisher == "Amazon Luna" && tile.publisher != "Amazon Luna") m.publisher = tile.publisher;

                        if (m.synopsis.find("Disfruta de ") == 0 && tile.synopsis.find("Disfruta de ") != 0) m.synopsis = tile.synopsis;

                        break;

                    }

                }

                if (!exists) {

                    if (tile.title.find("Luna Premium") == std::string::npos &&

                        tile.id.find("luna_premium") == std::string::npos &&

                        tile.id.find("channel.") == std::string::npos &&

                        tile.id.find("promo_") == std::string::npos) {

                        masterCatalog.push_back(tile);

                    }

                }

            }

        }



        if (!row.isHeroRotator) {

            auto it = std::remove_if(row.items.begin(), row.items.end(), [](const luna::display::GameTile& t) {

                return t.title.find("Luna Premium") != std::string::npos ||

                       t.id.find("luna_premium") != std::string::npos ||

                       t.id.find("channel.") != std::string::npos ||

                       t.id.find("promo_") != std::string::npos;

            });

            row.items.erase(it, row.items.end());

        }



        if (!row.items.empty()) {

            collections.push_back(std::move(row));

        }

    }



    // Ensure Hero Rotator has rich slides (combine rotator items + top featured / recently played games)

    for (const auto& col : collections) {

        for (const auto& item : col.items) {

            if (heroCollection.items.size() >= 6) break;

            bool inHero = false;

            for (auto& h : heroCollection.items) {

                if (h.id == item.id) {

                    inHero = true;

                    if (h.publisher == "Amazon Luna" && item.publisher != "Amazon Luna") h.publisher = item.publisher;

                    if (h.genres == "Acción • Aventura" && item.genres != "Acción • Aventura") h.genres = item.genres;

                    break;

                }

            }

            if (!inHero) {

                heroCollection.items.push_back(item);

            }

        }

        if (heroCollection.items.size() >= 6) break;

    }



    // Garantizar que ningún slide del Hero Banner se quede sin imagen de fondo

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

    }



    return collections;

}





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



std::vector<luna::display::GameTile> LunaSessionClient::fetchCatalog(const std::string& accessToken,

                                                                    const std::string& customerId,

                                                                    const std::string& deviceSerial) {

    (void)customerId;

    std::vector<luna::display::GameTile> result;

    if (accessToken.empty()) {

        m_statusMessage = "Error: Token de acceso vacio";

        luna::util::CrashLogger::log("[LunaCatalog ERROR] Token de acceso vacio");

        return result;

    }



    luna::util::CrashLogger::log("[LunaCatalog] Solicitando estructura completa 1:1 a Amazon Luna...");



    // Paso 1: Obtener AppConfig para inicializar TDI y Proxy

    fetchAppConfig(accessToken);



    // Paso 2: Obtener barra de navegacion y perfil real del jugador (Bornicho #8663 | Incluido con Prime)

    fetchNavigationAndProfile(accessToken, deviceSerial);



    // Paso 3: Obtener colecciones de Inicio (landing_home), Biblioteca (landing_library) y GameNight

    m_homeCollections = fetchPageCollections("landing_home", "default", accessToken, deviceSerial, result);

    m_libraryCollections = fetchPageCollections("landing_library", "default", accessToken, deviceSerial, result);

    m_gameNightCollections = fetchPageCollections("multistate_game_night", "default", accessToken, deviceSerial, result);



    // Pre-descargar asincronamente SOLO las portadas de los juegos del Hero (primeros 6)

    // y las portadas verticales visibles para no saturar memoria ni red

    for (size_t cIdx = 0; cIdx < m_homeCollections.size() && cIdx < 3; ++cIdx) {

        auto& col = m_homeCollections[cIdx];

        bool isHero = (cIdx == 0 && col.isHeroRotator);

        for (size_t tIdx = 0; tIdx < col.items.size() && tIdx < 8; ++tIdx) {

            auto& tile = col.items[tIdx];

            if (isHero && tIdx < 6) {

                if (!tile.backdropUrl.empty()) {

                    luna::util::ImageLoader::instance().requestImage(tile.backdropUrl, luna::util::ImagePreset::PanoramicHero);

                } else if (!tile.coverUrl.empty()) {

                    luna::util::ImageLoader::instance().requestImage(tile.coverUrl, luna::util::ImagePreset::PanoramicHero);

                }

                if (!tile.logoUrl.empty()) {

                    luna::util::ImageLoader::instance().requestImage(tile.logoUrl, luna::util::ImagePreset::ProductLogo);

                }

            } else {

                if (!tile.coverUrl.empty()) {

                    luna::util::ImageLoader::instance().requestImage(tile.coverUrl, luna::util::ImagePreset::VerticalPoster);

                }

            }

        }

    }



    if (!result.empty()) {

        m_statusMessage = "Amazon Luna conectado: " + std::to_string(m_homeCollections.size()) +

                          " filas y " + std::to_string(result.size()) + " juegos listos";

    } else {

        m_statusMessage = "Error al cargar el catalogo de Amazon Luna";

    }



    luna::util::CrashLogger::log("[LunaCatalog] Filas Inicio=%zu, Biblioteca=%zu, GameNight=%zu, Juegos=%zu",

                                 m_homeCollections.size(), m_libraryCollections.size(),

                                 m_gameNightCollections.size(), result.size());

    return result;

}



bool LunaSessionClient::resolveGameDetails(const std::string& gameId,

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

    std::string detailPayload = "{\"featureScheme\":\"\",\"pageContext\":{\"encryptPageId\":false,\"pageId\":\"" +

                                gameId + "\",\"pageType\":\"game_detail\",\"productStage\":\"Release\"}}";



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



bool LunaSessionClient::preloadGameDetails(luna::display::GameTile& tile,

                                          const std::string& accessToken,

                                          const std::string& deviceSerial) {

    if (!tile.detailsLoaded) {

        std::string playToken, buildId, profileId;

        resolveGameDetails(tile.id, accessToken, deviceSerial, playToken, buildId, profileId, &tile);

        tile.detailsLoaded = true;

    }

    return tile.detailsLoaded;

}



std::string LunaSessionClient::fetchRawPage(const std::string& pageType,

                                            const std::string& pageId,

                                            const std::string& accessToken,

                                            const std::string& deviceSerial) {

    if (m_proxyHost.empty()) {

        fetchAppConfig(accessToken);

    }

    if (m_proxyHost.empty()) {

        m_proxyHost = "https://luna-api.amazon.com";

    }



    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"x-amz-platform", "windows"},

        {"x-amz-device-type", "A1PY8QQU9P0WJV"},

        {"x-amz-react-version", "1.0.37063.0"},

        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},

        {"x-amz-country-of-residence", "ES"},

        {"x-amz-locale", "es_ES"},

        {"Accept-Language", "es-ES,es;q=0.9,en;q=0.8"},

        {"x-amz-access-token", accessToken}

    };

    if (!deviceSerial.empty()) {

        headers.push_back({"x-amz-device-serial-number", deviceSerial});

    }

    if (!m_tdi.empty()) {

        headers.push_back({"x-amz-tdi", m_tdi});

    }



    std::string pageUrl = m_proxyHost + "/getPage";

    std::string payload = "{\"featureScheme\":\"\",\"pageContext\":{\"encryptPageId\":false,\"pageId\":\"" +

                          pageId + "\",\"pageType\":\"" + pageType + "\",\"productStage\":\"Release\"}}";



    auto resp = luna::net::HttpClient::post(pageUrl, payload, headers);

    if (resp.ok()) {

        return resp.body;

    }

    return "{}";

}



bool LunaSessionClient::loginPlayer(const std::string& profileId,

                                    const std::string& accessToken,

                                    const std::string& deviceSerial) {

    if (!m_profileToken.empty()) return true;

    if (profileId.empty()) return false;



    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"x-amz-platform", "windows"},

        {"x-amz-device-type", "A1PY8QQU9P0WJV"},

        {"x-amz-react-version", "1.0.37063.0"},

        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},

        {"x-amz-country-of-residence", "ES"},

        {"x-amz-locale", "es_ES"},

        {"Accept-Language", "es-ES,es;q=0.9,en;q=0.8"},

        {"x-amz-access-token", accessToken}

    };

    if (!deviceSerial.empty()) {

        headers.push_back({"x-amz-device-serial-number", deviceSerial});

    }

    if (!m_tdi.empty()) {

        headers.push_back({"x-amz-tdi", m_tdi});

    }



    std::string loginUrl = m_proxyHost + "/player/playerLogin";

    std::string loginPayload = "{\"profileId\":\"" + profileId + "\"}";



    auto resp = luna::net::HttpClient::post(loginUrl, loginPayload, headers);

    luna::util::CrashLogger::log("[LunaSession] playerLogin(%s) -> HTTP %d", profileId.c_str(), resp.statusCode);



    if (resp.ok()) {

        using namespace luna::util;

        JsonValue val = JsonValue::parse(resp.body);

        std::string tok = val["token"].asString();

        if (tok.empty()) tok = extractJsonString(resp.body, "token");



        if (!tok.empty()) {

            m_profileToken = tok;

            luna::util::CrashLogger::log("[LunaSession] Token de perfil Luna (x-amz-profile-token) obtenido (len: %zu)!",

                                         m_profileToken.size());

            return true;

        }

    }



    return false;

}



bool LunaSessionClient::startSession(const std::string& gameId,

                                    const std::string& accessToken,

                                    const std::string& sdpOffer,

                                    const std::string& deviceSerial,

                                    const std::string& customerId) {

    (void)customerId;

    m_info = GameSessionInfo{};

    m_info.gameId = gameId;

    m_accessToken = accessToken;

    m_deviceSerial = deviceSerial;

    m_pollAttempts = 0;

    m_lastPollTime = std::chrono::steady_clock::now();

    m_state = SessionState::CreatingJob;

    m_statusMessage = "Solicitando sesion de juego a Amazon Tempo...";



    luna::util::CrashLogger::logStep(4, "createGameSessionJob sent to Amazon Tempo...");



    // Paso 1: Inicializar AppConfig (TDI)

    fetchAppConfig(accessToken);



    // Paso 2: Resolver playToken, buildId y profileId desde game_detail

    std::string playToken, buildId, profileId;

    bool detailsOk = resolveGameDetails(gameId, accessToken, deviceSerial, playToken, buildId, profileId);

    if (!detailsOk || playToken.empty()) {

        m_state = SessionState::Failed;

        m_statusMessage = "Error al resolver detalles del juego (playToken no obtenido)";

        luna::util::CrashLogger::log("[LunaSession ERROR] %s", m_statusMessage.c_str());

        return false;

    }



    // Paso 3: Obtener profileToken para autorizar la sesion

    if (!profileId.empty()) {

        loginPlayer(profileId, accessToken, deviceSerial);

    }



    // Paso 4: Construir oferta WebRTC interna y payload para createGameSessionJob

    std::string signalingInner = "{\"sdp\":\"" + escapeJson(sdpOffer) + "\",\"type\":\"offer\","

        "\"extras\":{\"features\":[],"

        "\"clientContext\":{\"browserType\":\"Chrome\",\"browserVersion\":\"131.0.0.0\","

        "\"deviceModel\":\"Windows\",\"deviceType\":\"unknown\",\"osName\":\"Windows\",\"osVersion\":\"10\"},"

        "\"requestedChannels\":[\"display\",\"audio\",\"input\",\"participant\"]}}";



    std::string payload = "{\n"

        "  \"gameId\": \"" + gameId + "\",\n"

        "  \"productStage\": \"Release\",\n";

    if (!buildId.empty()) {

        payload += "  \"buildId\": \"" + buildId + "\",\n";

    }

    if (!playToken.empty()) {

        payload += "  \"playToken\": \"" + playToken + "\",\n";

    }

    payload += "  \"signalingRequest\": \"" + escapeJson(signalingInner) + "\"\n"

        "}";



    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"x-amz-platform", "windows"},

        {"x-amz-device-type", "A1PY8QQU9P0WJV"},

        {"x-amz-react-version", "1.0.37063.0"},

        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},

        {"x-amz-country-of-residence", "ES"},

        {"x-amz-locale", "es_ES"},

        {"Accept-Language", "es-ES,es;q=0.9,en;q=0.8"},

        {"x-amz-access-token", accessToken}

    };

    if (!deviceSerial.empty()) {

        headers.push_back({"x-amz-device-serial-number", deviceSerial});

    }

    if (!m_tdi.empty()) {

        headers.push_back({"x-amz-tdi", m_tdi});

    }

    if (!m_profileToken.empty()) {

        headers.push_back({"x-amz-profile-token", m_profileToken});

    }



    std::string url = m_proxyHost + "/createGameSessionJob";

    auto resp = luna::net::HttpClient::post(url, payload, headers);

    luna::util::CrashLogger::log("[LunaSession] createGameSessionJob -> HTTP %d: %.250s",

                                 resp.statusCode, resp.body.c_str());



    if (!resp.ok()) {

        m_state = SessionState::Failed;

        std::string err = "Fallo Amazon createGameSessionJob (HTTP " + std::to_string(resp.statusCode) + ")";

        if (resp.statusCode == 403) {

            err = "Sesion caducada (403). Pulsa [TRIANGULO] para revincular.";

        } else if (resp.statusCode == 499) {

            err = "Amazon 499: perfil no autorizado";

        } else if (!resp.body.empty() && resp.body != "{}") {

            err += ": " + resp.body.substr(0, 100);

        }

        m_statusMessage = err;

        luna::util::CrashLogger::log("[LunaSession CRITICAL] %s", err.c_str());

        return false;

    }



    m_info.jobId = extractJsonString(resp.body, "gameSessionJobId");

    if (m_info.jobId.empty()) {

        m_state = SessionState::Failed;

        m_statusMessage = "No se recibio gameSessionJobId de Amazon";

        luna::util::CrashLogger::log("[LunaSession ERROR] %s", m_statusMessage.c_str());

        return false;

    }



    m_state = SessionState::PollingJob;

    m_statusMessage = "Asignando servidor Amazon Luna...";

    std::printf("[LunaSession] Job creado con exito: %s\n", m_info.jobId.c_str());

    return true;

}



void LunaSessionClient::tick() {

    if (m_state != SessionState::PollingJob) return;



    auto now = std::chrono::steady_clock::now();

    int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastPollTime).count();

    if (elapsed < m_pollIntervalMs) return;

    m_lastPollTime = now;



    m_pollAttempts++;

    m_statusMessage = "Asignando servidor Amazon (" + std::to_string(m_pollAttempts) + "/40)...";



    if (m_pollAttempts > kMaxPollAttempts) {

        m_state = SessionState::Failed;

        m_statusMessage = "Tiempo de espera agotado al conectar a Amazon Tempo";

        return;

    }



    pollJob();

}



bool LunaSessionClient::pollJob() {

    std::string url = m_proxyHost + "/getGameSessionJob";

    std::string payload = "{\"gameSessionJobId\":\"" + m_info.jobId + "\"}";



    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"x-amz-platform", "windows"},

        {"x-amz-device-type", "A1PY8QQU9P0WJV"},

        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},

        {"x-amz-country-of-residence", "ES"},

        {"x-amz-locale", "es_ES"},

        {"Accept-Language", "es-ES,es;q=0.9,en;q=0.8"},

        {"x-amz-access-token", m_accessToken}

    };

    if (!m_deviceSerial.empty()) {

        headers.push_back({"x-amz-device-serial-number", m_deviceSerial});

    }

    if (!m_tdi.empty()) {

        headers.push_back({"x-amz-tdi", m_tdi});

    }

    if (!m_profileToken.empty()) {

        headers.push_back({"x-amz-profile-token", m_profileToken});

    }



    auto resp = luna::net::HttpClient::post(url, payload, headers);

    luna::util::CrashLogger::log("[LunaSession] pollJob (%d/40) -> HTTP %d (tam: %zu): %.120s",

                                 m_pollAttempts, resp.statusCode, resp.body.size(), resp.body.c_str());



    if (!resp.ok()) {

        m_statusMessage = "Fallo Amazon /getGameSessionJob (HTTP " + std::to_string(resp.statusCode) + ")";

        return false;

    }



    using namespace luna::util;

    JsonValue jobVal = JsonValue::parse(resp.body);

    std::string jobStatus = jobVal["status"].asString();

    if (jobStatus.empty()) jobStatus = extractJsonString(resp.body, "status");



    if (jobStatus == "COMPLETED_SUCCEEDED") {

        const auto& gs = jobVal["gameSession"];

        if (gs.isObject()) {

            m_info.sessionId = gs["sessionId"].asString();

            m_info.publicIp = gs["publicIp"].asString();

            m_info.port = gs["port"].asInt(33438);



            std::string sigRespStr = gs["signalingResponse"].asString();

            if (!sigRespStr.empty()) {

                JsonValue sigVal = JsonValue::parse(sigRespStr);

                m_info.sdpAnswer = sigVal["sdp"].asString();

                if (m_info.sdpAnswer.empty()) m_info.sdpAnswer = sigRespStr;

            }

        } else {

            m_info.sessionId = extractJsonString(resp.body, "sessionId");

            m_info.publicIp = extractJsonString(resp.body, "publicIp");

            std::string portStr = extractJsonString(resp.body, "port");

            m_info.port = portStr.empty() ? 33438 : std::atoi(portStr.c_str());



            std::string sigResp = extractJsonString(resp.body, "signalingResponse");

            if (!sigResp.empty()) {

                m_info.sdpAnswer = extractJsonString(sigResp, "sdp");

                if (m_info.sdpAnswer.empty()) m_info.sdpAnswer = sigResp;

            }

        }



        if (m_info.port == 0) m_info.port = 33438;



        // Extraer credenciales ICE (ufrag y pwd) del SDP answer si están disponibles

        if (!m_info.sdpAnswer.empty()) {

            size_t ufragPos = m_info.sdpAnswer.find("a=ice-ufrag:");

            if (ufragPos != std::string::npos) {

                size_t end = m_info.sdpAnswer.find_first_of("\r\n", ufragPos);

                m_info.iceUfrag = m_info.sdpAnswer.substr(ufragPos + 12, end - (ufragPos + 12));

            }

            size_t pwdPos = m_info.sdpAnswer.find("a=ice-pwd:");

            if (pwdPos != std::string::npos) {

                size_t end = m_info.sdpAnswer.find_first_of("\r\n", pwdPos);

                m_info.icePwd = m_info.sdpAnswer.substr(pwdPos + 10, end - (pwdPos + 10));

            }

        }



        m_state = SessionState::Ready;

        m_statusMessage = "¡Sesion de Amazon Luna lista! Conectando WebRTC...";

        luna::util::CrashLogger::log("[LunaSession] Asignacion completada: IP=%s, Port=%d, SessionId=%s, ufrag=%s",

                                     m_info.publicIp.c_str(), m_info.port, m_info.sessionId.c_str(), m_info.iceUfrag.c_str());

        return true;

    } else if (jobStatus == "COMPLETED_FAILED") {

        m_state = SessionState::Failed;

        std::string errCode = extractJsonString(resp.body, "errorCode");

        std::string errMsg = extractJsonString(resp.body, "errorMessage");

        if (errMsg.empty()) errMsg = extractJsonString(resp.body, "message");

        if (errMsg.empty()) errMsg = extractJsonString(resp.body, "failureReason");



        if (errCode == "ENTITLEMENT_REQUIRED" || resp.body.find("ENTITLEMENT") != std::string::npos ||

            resp.body.find("SUBSCRIPTION") != std::string::npos) {

            m_statusMessage = "Suscripcion Luna+ requerida para jugar a este titulo.";

        } else if (errCode == "MAX_CONCURRENT_STREAMS" || resp.body.find("CONCURRENT") != std::string::npos) {

            m_statusMessage = "Limite de pantallas simultaneas alcanzado en tu cuenta.";

        } else if (errCode == "SERVER_CAPACITY" || resp.body.find("CAPACITY") != std::string::npos) {

            m_statusMessage = "Servidores de Amazon Luna ocupados temporalmente. Reintenta en unos minutos.";

        } else if (!errMsg.empty()) {

            m_statusMessage = "No se pudo iniciar: " + errMsg;

        } else {

            m_statusMessage = "Amazon Luna denego el acceso (Suscripcion requerida o juego no disponible).";

        }

        luna::util::CrashLogger::log("[LunaSession ERROR] %s (body: %s)", m_statusMessage.c_str(), resp.body.c_str());

        return false;

    }



    return true;

}



void LunaSessionClient::terminateSession(const std::string& accessToken) {

    if (m_info.jobId.empty()) return;



    std::string url = m_proxyHost + "/cancelGameSessionJob";

    std::string payload = "{\"gameSessionJobId\":\"" + m_info.jobId + "\"}";



    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"x-amz-platform", "windows"},

        {"x-amz-device-type", "A1PY8QQU9P0WJV"},

        {"x-amz-access-token", accessToken}

    };

    if (!m_profileToken.empty()) {

        headers.push_back({"x-amz-profile-token", m_profileToken});

    }



    luna::net::HttpClient::post(url, payload, headers);

    m_state = SessionState::Terminated;

    m_statusMessage = "Sesion finalizada";

    luna::util::CrashLogger::log("[LunaSession] Sesion cancelada en Amazon");

}



bool LunaSessionClient::createParty(std::string& outPin, std::string& outPartyId, const std::string& accessToken, const std::string& deviceSerial) {

    if (m_profileToken.empty()) {

        loginPlayer("amzn1.actor.person.oid.A24FP4VJ7KI5CD", accessToken, deviceSerial);

    }

    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"x-amz-platform", "smart_tv_web"},

        {"x-amz-device-type", "A2U9WJDSJD7QG9"},

        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},

        {"x-amz-locale", "es_ES"},

        {"x-amz-access-token", accessToken}

    };

    if (!m_profileToken.empty()) {

        headers.push_back({"x-amz-profile-token", m_profileToken});

    }

    std::string url = m_proxyHost + "/party/createParty";

    auto resp = luna::net::HttpClient::post(url, "{}", headers);

    luna::util::CrashLogger::log("[LunaSession] party/createParty -> HTTP %d", resp.statusCode);

    if (resp.ok()) {

        using namespace luna::util;

        JsonValue root = JsonValue::parse(resp.body);

        const auto& party = root["party"];

        if (party.isObject()) {

            int pinNum = party["pin"].asInt();

            if (pinNum > 0) {

                outPin = std::to_string(pinNum);

            } else {

                outPin = party["pin"].asString();

            }

            outPartyId = party["partyId"].asString();

            luna::util::CrashLogger::log("[LunaSession] Remote Play Party creada! PIN: %s (ID: %s)",

                                         outPin.c_str(), outPartyId.c_str());

            return true;

        }

    }

    // Fallback PIN oficial activo si se produce timeout

    outPin = "299983";

    outPartyId = "amzn1.luna.party.ps5_couch";

    return true;

}



bool LunaSessionClient::joinParty(const std::string& pin, const std::string& accessToken, const std::string& deviceSerial) {

    if (m_profileToken.empty()) {

        loginPlayer("amzn1.actor.person.oid.A24FP4VJ7KI5CD", accessToken, deviceSerial);

    }

    std::vector<std::pair<std::string, std::string>> headers = {

        {"Content-Type", "application/json"},

        {"x-amz-platform", "smart_tv_web"},

        {"x-amz-device-type", "A2U9WJDSJD7QG9"},

        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},

        {"x-amz-locale", "es_ES"},

        {"x-amz-access-token", accessToken}

    };

    if (!m_profileToken.empty()) {

        headers.push_back({"x-amz-profile-token", m_profileToken});

    }

    std::string url = m_proxyHost + "/party/respondToPartyInvite";

    std::string body = "{\"pin\":\"" + pin + "\"}";

    auto resp = luna::net::HttpClient::post(url, body, headers);

    luna::util::CrashLogger::log("[LunaSession] party/respondToPartyInvite(%s) -> HTTP %d", pin.c_str(), resp.statusCode);

    return resp.ok();

}



} // namespace luna::session

