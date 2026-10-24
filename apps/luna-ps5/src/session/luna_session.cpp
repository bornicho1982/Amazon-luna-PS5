#include "session/luna_session.h"
#include "net/http_client.h"
#include "util/crash_logger.h"
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
    static const std::string b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string in = input;
    for (char& c : in) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (in.size() % 4 != 0) {
        in += '=';
    }

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

} // namespace

LunaSessionClient::LunaSessionClient() = default;
LunaSessionClient::~LunaSessionClient() = default;

bool LunaSessionClient::fetchAppConfig(const std::string& accessToken) {
    if (!m_tdi.empty()) return true;

    std::string url = m_proxyHost + "/getAppConfig";
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"x-amz-platform", "smart_tv_web"},
        {"x-amz-device-type", "A2U9WJDSJD7QG9"},
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

std::vector<luna::display::GameTile> LunaSessionClient::fetchCatalog(const std::string& accessToken,
                                                                    const std::string& customerId,
                                                                    const std::string& deviceSerial) {
    std::vector<luna::display::GameTile> result;
    if (accessToken.empty()) {
        m_statusMessage = "Error: Token de acceso vacio";
        luna::util::CrashLogger::log("[LunaCatalog ERROR] Token de acceso vacio");
        return result;
    }

    luna::util::CrashLogger::log("[LunaCatalog] Solicitando catalogo dinamico a Amazon Luna...");

    // Paso 1: Obtener AppConfig para inicializar TDI y Proxy
    fetchAppConfig(accessToken);

    // Paso 2: Encabezados validados contra backend Coral Amazon Luna
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"Accept", "application/json"},
        {"x-amz-platform", "smart_tv_web"},
        {"x-amz-device-type", "A2U9WJDSJD7QG9"},
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

    // Paso 3: Solicitar landing_home con featureScheme vacio ("")
    std::string pageUrl = m_proxyHost + "/getPage";
    std::string homePayload = "{\"featureScheme\":\"\",\"serviceToken\":\"eyJlbmNyeXB0UGFnZUlkIjpmYWxzZSwicGFnZUlkIjoiZGVmYXVsdCIsInBhZ2VUeXBlIjoibGFuZGluZ19ob21lIn0=\"}";

    auto resp = luna::net::HttpClient::post(pageUrl, homePayload, headers);
    luna::util::CrashLogger::log("[LunaCatalog] getPage (landing_home) -> HTTP %d (tam: %zu)",
                                 resp.statusCode, resp.body.size());

    static const uint32_t kCardColors[] = {
        0xFF0284C7, // Sky Blue
        0xFFF59E0B, // Amber
        0xFF10B981, // Emerald
        0xFF06B6D4, // Cyan
        0xFFEF4444, // Red
        0xFF8B5CF6, // Purple
        0xFFEC4899, // Pink
        0xFF14B8A6, // Teal
        0xFFF97316, // Orange
        0xFF6366F1, // Indigo
        0xFF84CC16, // Lime
        0xFF3B82F6  // Blue
    };

    if (resp.ok() && !resp.body.empty()) {
        using namespace luna::util;
        JsonValue root = JsonValue::parse(resp.body);
        const auto& widgets = root["pageMemberGroups"]["mainContent"]["widgets"];

        if (widgets.isArray()) {
            for (size_t i = 0; i < widgets.size(); ++i) {
                const auto& col = widgets[i];
                std::string colId = col["id"].asString();
                const auto& subWidgets = col["widgets"];
                if (!subWidgets.isArray()) continue;

                for (size_t j = 0; j < subWidgets.size(); ++j) {
                    const auto& sw = subWidgets[j];
                    std::string swId = sw["id"].asString();

                    std::string cleanGid;
                    size_t lunaPos = swId.find("amzn1.luna.product.");
                    size_t adgPos = swId.find("amzn1.adg.product.");
                    size_t prodPos = (lunaPos != std::string::npos) ? lunaPos : adgPos;
                    if (prodPos != std::string::npos) {
                        size_t ampPos = swId.find_first_of("& \t\r\n\\", prodPos);
                        cleanGid = (ampPos != std::string::npos) ? swId.substr(prodPos, ampPos - prodPos) : swId.substr(prodPos);
                    } else {
                        const auto& acts = sw["actions"];
                        if (acts.isArray()) {
                            for (size_t a = 0; a < acts.size(); ++a) {
                                std::string target = acts[a]["target"].asString();
                                size_t tLuna = target.find("amzn1.luna.product.");
                                size_t tAdg = target.find("amzn1.adg.product.");
                                size_t tPos = (tLuna != std::string::npos) ? tLuna : tAdg;
                                if (tPos != std::string::npos) {
                                    size_t idLen = (tLuna != std::string::npos) ? 55 : 54;
                                    cleanGid = target.substr(tPos, idLen);
                                    break;
                                }
                            }
                        }
                    }

                    if (cleanGid.empty()) continue;

                    // Filtrar duplicados
                    bool dup = false;
                    for (const auto& item : result) {
                        if (item.id == cleanGid) { dup = true; break; }
                    }
                    if (dup) continue;

                    std::string pdataStr = sw["presentationData"].asString();
                    JsonValue pdata = JsonValue::parse(pdataStr);

                    std::string title = pdata["title"].asString();
                    if (title.empty()) title = pdata["name"].asString();
                    if (title.empty()) title = pdata["heading"].asString();
                    if (title.empty()) continue;

                    std::string coverUrl = pdata["boxArtUrl"].asString();
                    if (coverUrl.empty()) coverUrl = pdata["imagePortrait"].asString();
                    if (coverUrl.empty()) coverUrl = pdata["imageLandscape"].asString();
                    if (coverUrl.empty()) coverUrl = pdata["thumbnailUrl"].asString();

                    std::string publisher = pdata["publisher"].asString();
                    if (publisher.empty()) publisher = pdata["developer"].asString();
                    if (publisher.empty()) publisher = pdata["publisherName"].asString();

                    std::string synopsis = pdata["synopsis"].asString();
                    if (synopsis.empty()) synopsis = pdata["shortDescription"].asString();
                    if (synopsis.empty()) synopsis = pdata["description"].asString();

                    std::string category = pdata["category"].asString();
                    if (category.empty()) category = pdata["badgeText"].asString();
                    if (category.empty()) {
                        if (colId.find("recently_played") != std::string::npos) category = "MI BIBLIOTECA";
                        else if (colId.find("popular") != std::string::npos) category = "DESTACADOS";
                        else if (colId.find("luna_standard") != std::string::npos) category = "LUNA+";
                        else if (colId.find("editors_pick") != std::string::npos) category = "SELECCION";
                        else if (colId.find("adventure") != std::string::npos) category = "AVENTURA";
                        else if (colId.find("highlight") != std::string::npos) category = "PRIME GAMING";
                        else category = "LUNA+";
                    }

                    std::string pegi = pdata["pegiRating"].asString();

                    luna::display::GameTile tile;
                    tile.id = cleanGid;
                    tile.title = title;
                    tile.publisher = publisher.empty() ? "Amazon Luna" : publisher;
                    tile.synopsis = synopsis.empty() ? "Disponible para jugar al instante en Amazon Luna Cloud." : synopsis;
                    tile.category = category.empty() ? "LUNA+" : category;
                    tile.pegiRating = pegi.empty() ? "PEGI 3" : pegi;
                    tile.coverUrl = coverUrl;

                    uint32_t colColor = kCardColors[result.size() % 12];
                    tile.badgeColor = colColor;
                    tile.heroColor = colColor;

                    // Descargar y decodificar portada en memoria para las primeras 6 tarjetas visibles
                    if (result.size() < 6 && !tile.coverUrl.empty() && tile.coverUrl.rfind("http", 0) == 0) {
                        auto imgResp = luna::net::HttpClient::get(tile.coverUrl);
                        if (imgResp.ok() && !imgResp.body.empty()) {
                            int w = 0, h = 0;
                            if (luna::util::decodeImageRgba(reinterpret_cast<const uint8_t*>(imgResp.body.data()),
                                                            imgResp.body.size(), tile.coverRgba, w, h)) {
                                tile.coverWidth = w;
                                tile.coverHeight = h;
                            }
                        }
                    }

                    result.push_back(std::move(tile));
                    luna::util::CrashLogger::log("[LunaCatalog] Juego detectado: %s (%s)", title.c_str(), cleanGid.c_str());
                }
            }
        }
    }

    if (!result.empty()) {
        m_statusMessage = "Catalogo cargado: " + std::to_string(result.size()) + " juegos de Amazon Luna";
    } else if (resp.statusCode > 0) {
        m_statusMessage = "Error Amazon /getPage (HTTP " + std::to_string(resp.statusCode) + ")";
        if (resp.statusCode == 403) {
            m_statusMessage += " [Sesion caducada o token no renovado]";
        } else if (resp.statusCode == 500) {
            m_statusMessage += " [Error interno Amazon/Token invalido]";
        }
        if (!resp.body.empty()) {
            std::string sub = resp.body.substr(0, 80);
            for (char& c : sub) { if (c == '\r' || c == '\n') c = ' '; }
            m_statusMessage += " " + sub;
        }
    } else {
        m_statusMessage = "Error de red al conectar con Amazon Luna: " + resp.error;
    }

    luna::util::CrashLogger::log("[LunaCatalog] Total de juegos dinamicos cargados: %zu - %s",
                                 result.size(), m_statusMessage.c_str());
    return result;
}

bool LunaSessionClient::resolveGameDetails(const std::string& gameId,
                                          const std::string& accessToken,
                                          const std::string& deviceSerial,
                                          std::string& outPlayToken,
                                          std::string& outBuildId,
                                          std::string& outProfileId) {
    fetchAppConfig(accessToken);

    // Consulta de game_detail bajo perfil Windows para obtener playToken y buildId sin restricciones de OS
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
    std::string detailPayload = "{\"featureScheme\":\"\",\"pageContext\":{\"encryptPageId\":false,\"pageId\":\"" +
                                gameId + "\",\"pageType\":\"game_detail\",\"productStage\":\"Release\"}}";

    auto resp = luna::net::HttpClient::post(pageUrl, detailPayload, headers);
    luna::util::CrashLogger::log("[LunaSession] getPage (game_detail) -> HTTP %d (tam: %zu)",
                                 resp.statusCode, resp.body.size());

    if (resp.ok()) {
        using namespace luna::util;
        JsonValue root = JsonValue::parse(resp.body);

        // Buscar action GAME_SESSION_ASYNC en los widgets de la respuesta
        const auto& widgets = root["pageMemberGroups"]["mainContent"]["widgets"];
        if (widgets.isArray()) {
            for (size_t w = 0; w < widgets.size(); ++w) {
                const auto& widget = widgets[w];
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
                        if (!outPlayToken.empty()) break;
                    }
                }
                if (!outPlayToken.empty()) break;
            }
        }

        // Fallback: si no vino en estructura de widgets, buscar en campos planos
        if (outPlayToken.empty()) {
            outPlayToken = extractJsonString(resp.body, "playToken");
            outBuildId = extractJsonString(resp.body, "buildId");
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

        luna::util::CrashLogger::log("[LunaSession] resolveGameDetails exito: playToken len=%zu, buildId=%s, profileId=%s",
                                     outPlayToken.size(), outBuildId.c_str(), outProfileId.c_str());
        return !outPlayToken.empty();
    }

    return false;
}

bool LunaSessionClient::preloadGameDetails(luna::display::GameTile& tile,
                                          const std::string& accessToken,
                                          const std::string& deviceSerial) {
    if (tile.detailsLoaded) return true;

    std::string playToken, buildId, profileId;
    bool ok = resolveGameDetails(tile.id, accessToken, deviceSerial, playToken, buildId, profileId);
    if (ok) {
        tile.detailsLoaded = true;
    }

    // Descargar carátula si aún no está en memoria
    if (tile.coverRgba.empty() && !tile.coverUrl.empty() && tile.coverUrl.rfind("http", 0) == 0) {
        auto imgResp = luna::net::HttpClient::get(tile.coverUrl);
        if (imgResp.ok() && !imgResp.body.empty()) {
            int w = 0, h = 0;
            if (luna::util::decodeImageRgba(reinterpret_cast<const uint8_t*>(imgResp.body.data()),
                                            imgResp.body.size(), tile.coverRgba, w, h)) {
                tile.coverWidth = w;
                tile.coverHeight = h;
            }
        }
    }
    return ok;
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
    std::string signalingInner = "{\"type\":\"offer\",\"sdp\":\"" + escapeJson(sdpOffer) + "\","
        "\"extras\":{\"features\":{},\"requestedChannels\":[\"display\",\"audio\",\"input\",\"participant\"],"
        "\"clientContext\":{\"browserClientRole\":\"browser\",\"browserType\":\"Chrome\",\"browserVersion\":\"120.0.0.0\"}}}";

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

} // namespace luna::session
