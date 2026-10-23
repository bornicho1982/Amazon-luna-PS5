#include "session/luna_session.h"
#include "net/http_client.h"
#include "util/crash_logger.h"
#include "util/image_loader.h"

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
        const std::string& body = resp.body;
        size_t searchPos = 0;
        const std::string needle = "amzn1.";

        while (result.size() < 12 && (searchPos = body.find(needle, searchPos)) != std::string::npos) {
            size_t idEnd = body.find_first_of("\"' \t\r\n,}", searchPos);
            if (idEnd == std::string::npos) break;
            std::string gameId = body.substr(searchPos, idEnd - searchPos);

            // Filtrar solo identificadores de producto validos
            if (gameId.find(".product.") == std::string::npos) {
                searchPos = idEnd;
                continue;
            }

            bool dup = false;
            for (const auto& item : result) {
                if (item.id == gameId) { dup = true; break; }
            }
            if (dup) {
                searchPos = idEnd;
                continue;
            }

            // Ventana contextual para metadatos del juego
            size_t winStart = (searchPos > 1500) ? (searchPos - 1500) : 0;
            size_t winEnd = std::min(body.size(), searchPos + 1500);
            std::string window = body.substr(winStart, winEnd - winStart);

            std::string title = extractJsonString(window, "title");
            if (title.empty()) title = extractJsonString(window, "productTitle");
            if (title.empty()) title = extractJsonString(window, "name");

            std::string publisher = extractJsonString(window, "publisher");
            if (publisher.empty()) publisher = extractJsonString(window, "developer");
            if (publisher.empty()) publisher = extractJsonString(window, "publisherName");

            std::string synopsis = extractJsonString(window, "synopsis");
            if (synopsis.empty()) synopsis = extractJsonString(window, "shortDescription");
            if (synopsis.empty()) synopsis = extractJsonString(window, "description");

            std::string category = extractJsonString(window, "genreTags");
            if (category.empty()) category = extractJsonString(window, "badgeText");
            if (category.empty()) category = extractJsonString(window, "category");

            std::string coverUrl = extractJsonString(window, "imagePortrait");
            if (coverUrl.empty()) coverUrl = extractJsonString(window, "imageLandscape");
            if (coverUrl.empty()) coverUrl = extractJsonString(window, "boxArtUrl");
            if (coverUrl.empty()) coverUrl = extractJsonString(window, "gameIcon");
            if (coverUrl.empty()) coverUrl = extractJsonString(window, "thumbnailUrl");

            if (!title.empty()) {
                luna::display::GameTile tile;
                tile.id = gameId;
                tile.title = title;
                tile.publisher = publisher.empty() ? "Amazon Luna" : publisher;
                tile.synopsis = synopsis.empty() ? "Disponible para jugar al instante en Amazon Luna Cloud." : synopsis;
                tile.category = category.empty() ? "LUNA+" : category;
                uint32_t col = kCardColors[result.size() % 12];
                tile.badgeColor = col;
                tile.heroColor = col;
                tile.coverUrl = coverUrl;

                // Descargar y decodificar portada en memoria para los primeros 6 juegos
                if (result.size() < 6 && !tile.coverUrl.empty() && tile.coverUrl.rfind("http", 0) == 0) {
                    luna::util::CrashLogger::log("[LunaCatalog] Descargando portada para '%s' (%.60s)...",
                                                 tile.title.c_str(), tile.coverUrl.c_str());
                    auto imgResp = luna::net::HttpClient::get(tile.coverUrl);
                    if (imgResp.ok() && !imgResp.body.empty()) {
                        int w = 0, h = 0;
                        if (luna::util::decodeImageRgba(reinterpret_cast<const uint8_t*>(imgResp.body.data()),
                                                        imgResp.body.size(), tile.coverRgba, w, h)) {
                            tile.coverWidth = w;
                            tile.coverHeight = h;
                            luna::util::CrashLogger::log("[LunaCatalog] Portada decodificada con exito: %dx%d (%zu bytes RGBA)",
                                                         w, h, tile.coverRgba.size());
                        }
                    }
                }

                result.push_back(std::move(tile));
                luna::util::CrashLogger::log("[LunaCatalog] Juego detectado: %s (%s)", title.c_str(), gameId.c_str());
            }

            searchPos = idEnd;
        }
    }

    if (!result.empty()) {
        m_statusMessage = "Catálogo cargado: " + std::to_string(result.size()) + " juegos de Amazon Luna";
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
        outPlayToken = extractJsonString(resp.body, "playToken");
        outBuildId = extractJsonString(resp.body, "buildId");
        outProfileId = extractJsonString(resp.body, "profileId");

        // Si profileId no aparece como campo directo, extraerlo del JWT playToken (segundo segmento base64)
        if (outProfileId.empty() && !outPlayToken.empty()) {
            size_t dot1 = outPlayToken.find('.');
            if (dot1 != std::string::npos) {
                size_t dot2 = outPlayToken.find('.', dot1 + 1);
                if (dot2 != std::string::npos) {
                    std::string claimsB64 = outPlayToken.substr(dot1 + 1, dot2 - (dot1 + 1));
                    std::string claimsJson = base64UrlDecode(claimsB64);
                    outProfileId = extractJsonString(claimsJson, "profileId");
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
        std::string tok = extractJsonString(resp.body, "token");
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

    std::string jobStatus = extractJsonString(resp.body, "status");
    if (jobStatus == "COMPLETED_SUCCEEDED") {
        m_info.sessionId = extractJsonString(resp.body, "sessionId");
        m_info.publicIp = extractJsonString(resp.body, "publicIp");
        std::string portStr = extractJsonString(resp.body, "port");
        if (!portStr.empty()) {
            m_info.port = std::stoi(portStr);
        }

        std::string sigResp = extractJsonString(resp.body, "signalingResponse");
        if (!sigResp.empty()) {
            m_info.sdpAnswer = extractJsonString(sigResp, "sdp");
            if (m_info.sdpAnswer.empty()) {
                m_info.sdpAnswer = sigResp;
            }
        }

        m_state = SessionState::Ready;
        m_statusMessage = "¡Sesion de Amazon Luna lista! Conectando WebRTC...";
        luna::util::CrashLogger::log("[LunaSession] Asignacion completada: IP=%s, Port=%d, SessionId=%s",
                                     m_info.publicIp.c_str(), m_info.port, m_info.sessionId.c_str());
        return true;
    } else if (jobStatus == "COMPLETED_FAILED") {
        m_state = SessionState::Failed;
        m_statusMessage = "Amazon Luna indico fallo al asignar servidor para este juego";
        luna::util::CrashLogger::log("[LunaSession ERROR] %s", m_statusMessage.c_str());
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
