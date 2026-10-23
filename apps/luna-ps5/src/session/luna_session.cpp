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

std::string unescapeJson(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            char next = str[i + 1];
            if (next == '"') { out += '"'; i++; }
            else if (next == '\\') { out += '\\'; i++; }
            else if (next == 'r') { out += '\r'; i++; }
            else if (next == 'n') { out += '\n'; i++; }
            else if (next == 't') { out += '\t'; i++; }
            else { out += str[i]; }
        } else {
            out += str[i];
        }
    }
    return out;
}

} // namespace

LunaSessionClient::LunaSessionClient() = default;
LunaSessionClient::~LunaSessionClient() = default;

std::string LunaSessionClient::resolvePlayToken(const std::string& gameId,
                                                const std::vector<std::pair<std::string, std::string>>& headers) {
    std::string pageUrl = m_proxyHost + "/getPage";

    // 1. Consultar pagina provision_game con pageUri
    std::string payload1 = "{\"pageContext\":{\"pageId\":\"" + gameId + "\",\"pageUri\":\"provision_game\",\"productStage\":\"Release\"}}";
    auto resp1 = luna::net::HttpClient::post(pageUrl, payload1, headers);
    std::printf("[LunaSession] getPage(provision_game uri, %s) -> HTTP %d (tam: %zu)\n",
                gameId.c_str(), resp1.statusCode, resp1.body.size());

    if (resp1.ok()) {
        std::string tok = extractJsonString(resp1.body, "playToken");
        if (!tok.empty()) {
            std::printf("[LunaSession] playToken detectado en provision_game!\n");
            return tok;
        }
    }

    // 2. Consultar con pageType: provision_game
    std::string payload2 = "{\"pageContext\":{\"pageId\":\"" + gameId + "\",\"pageType\":\"provision_game\",\"productStage\":\"Release\"}}";
    auto resp2 = luna::net::HttpClient::post(pageUrl, payload2, headers);
    std::printf("[LunaSession] getPage(provision_game type, %s) -> HTTP %d (tam: %zu)\n",
                gameId.c_str(), resp2.statusCode, resp2.body.size());

    if (resp2.ok()) {
        std::string tok = extractJsonString(resp2.body, "playToken");
        if (!tok.empty()) {
            std::printf("[LunaSession] playToken detectado en provision_game type!\n");
            return tok;
        }
    }

    // 3. Fallback: Consultar pagina multistate_detail
    std::string payload3 = "{\"pageContext\":{\"pageId\":\"" + gameId + "\",\"pageType\":\"multistate_detail\"}}";
    auto resp3 = luna::net::HttpClient::post(pageUrl, payload3, headers);
    std::printf("[LunaSession] getPage(multistate_detail, %s) -> HTTP %d (tam: %zu)\n",
                gameId.c_str(), resp3.statusCode, resp3.body.size());

    if (resp3.ok()) {
        std::string tok = extractJsonString(resp3.body, "playToken");
        if (!tok.empty()) {
            std::printf("[LunaSession] playToken detectado en multistate_detail!\n");
            return tok;
        }
    }

    return "";
}

std::string LunaSessionClient::resolveProfileToken(std::vector<std::pair<std::string, std::string>>& headers,
                                                   const std::string& customerId) {
    if (!m_profileToken.empty()) return m_profileToken;

    // 1. Obtener TDI para telemetria IoT Amazon
    std::string iotUrl = m_proxyHost + "/getIotConnectionInfo";
    auto iotResp = luna::net::HttpClient::post(iotUrl, "{}", headers);
    std::printf("[LunaSession] getIotConnectionInfo -> HTTP %d (tam: %zu)\n",
                iotResp.statusCode, iotResp.body.size());
    if (iotResp.ok()) {
        std::string tdi = extractJsonString(iotResp.body, "tdi");
        if (!tdi.empty()) {
            headers.push_back({"x-amz-tdi", tdi});
            std::printf("[LunaSession] Cabecera x-amz-tdi configurada con exito\n");
        }
    }

    std::string loginUrl = m_proxyHost + "/player/playerLogin";

    // 2. Intentar playerLogin con customerId
    if (!customerId.empty()) {
        std::string loginPayload = "{\"profileId\":\"" + customerId + "\"}";
        auto lResp = luna::net::HttpClient::post(loginUrl, loginPayload, headers);
        std::printf("[LunaSession] playerLogin(%s) -> HTTP %d, body: %s\n",
                    customerId.c_str(), lResp.statusCode, lResp.body.c_str());
        if (lResp.ok()) {
            std::string tok = extractJsonString(lResp.body, "token");
            if (!tok.empty()) {
                m_profileToken = tok;
                std::printf("[LunaSession] Token de perfil Luna (x-amz-profile-token) obtenido con exito!\n");
                return m_profileToken;
            }
        }
    }

    // 3. Intentar playerLogin con "default"
    {
        std::string loginPayload = "{\"profileId\":\"default\"}";
        auto lResp = luna::net::HttpClient::post(loginUrl, loginPayload, headers);
        std::printf("[LunaSession] playerLogin(default) -> HTTP %d, body: %s\n",
                    lResp.statusCode, lResp.body.c_str());
        if (lResp.ok()) {
            std::string tok = extractJsonString(lResp.body, "token");
            if (!tok.empty()) {
                m_profileToken = tok;
                std::printf("[LunaSession] Token de perfil Luna adquirido via playerLogin(default)!\n");
                return m_profileToken;
            }
        }
    }

    // 4. Intentar playerLogin con payload vacio {}
    {
        auto lRespEmpty = luna::net::HttpClient::post(loginUrl, "{}", headers);
        std::printf("[LunaSession] playerLogin({}) -> HTTP %d, body: %s\n",
                    lRespEmpty.statusCode, lRespEmpty.body.c_str());
        if (lRespEmpty.ok()) {
            std::string tok = extractJsonString(lRespEmpty.body, "token");
            if (!tok.empty()) {
                m_profileToken = tok;
                std::printf("[LunaSession] Token de perfil adquirido via playerLogin({})!\n");
                return m_profileToken;
            }
        }
    }

    // 5. Fallback: createPlayerFromProfile
    if (!customerId.empty()) {
        std::string createUrl = m_proxyHost + "/player/createPlayerFromProfile";
        std::string createPayload = "{\"profileId\":\"" + customerId + "\"}";
        auto cResp = luna::net::HttpClient::post(createUrl, createPayload, headers);
        std::printf("[LunaSession] createPlayerFromProfile(%s) -> HTTP %d: %s\n",
                    customerId.c_str(), cResp.statusCode, cResp.body.c_str());
        if (cResp.ok()) {
            std::string tok = extractJsonString(cResp.body, "token");
            if (!tok.empty()) {
                m_profileToken = tok;
                return m_profileToken;
            }
        }
    }

    return "";
}

std::vector<luna::display::GameTile> LunaSessionClient::fetchCatalog(const std::string& accessToken,
                                                                    const std::string& customerId,
                                                                    const std::string& deviceSerial) {
    std::vector<luna::display::GameTile> result;
    if (accessToken.empty()) return result;

    luna::util::CrashLogger::log("[LunaCatalog] Solicitando catalogo dinamico a Amazon Luna...");

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"Accept", "application/json"},
        {"x-amz-access-token", accessToken},
        {"x-amz-platform", "smart_tv_web"},
        {"x-amz-device-type", "A2U9WJDSJD7QG9"},
        {"x-amz-device-model", "LG TV"},
        {"x-amz-client-version", "1.0.37063.0"},
        {"x-amz-react-version", "1.0.37063.0"},
        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},
        {"x-amz-country-of-residence", "ES"},
        {"x-amz-timezone", "Europe/Madrid"},
        {"x-amz-locale", "es-ES"},
        {"Accept-Language", "es-ES"},
        {"Origin", "https://luna.amazon.es"},
        {"Referer", "https://luna.amazon.es/"},
        {"User-Agent", "Mozilla/5.0 (Web0S; SmartTV) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 WebAppManager"}
    };

    if (!deviceSerial.empty()) {
        headers.push_back({"x-amz-device-serial-number", deviceSerial});
    }

    resolveProfileToken(headers, customerId);
    if (!m_profileToken.empty()) {
        headers.push_back({"x-amz-profile-token", m_profileToken});
    }

    std::string pageUrl = m_proxyHost + "/getPage";

    // 1. Probar con pageUri: home
    std::string pagePayload = "{\"pageContext\":{\"pageId\":\"default\",\"pageUri\":\"home\",\"productStage\":\"Release\"}}";
    auto resp = luna::net::HttpClient::post(pageUrl, pagePayload, headers);
    std::printf("[LunaCatalog] getPage(pageUri: home) -> HTTP %d (tam: %zu)\n", resp.statusCode, resp.body.size());

    // 2. Fallback con pageUri simple
    if (!resp.ok() || resp.body.empty() || resp.body.find("amzn1.luna.product.") == std::string::npos) {
        pagePayload = "{\"pageContext\":{\"pageId\":\"default\",\"pageUri\":\"home\"}}";
        resp = luna::net::HttpClient::post(pageUrl, pagePayload, headers);
        std::printf("[LunaCatalog] getPage(pageUri: home simple) -> HTTP %d (tam: %zu)\n", resp.statusCode, resp.body.size());
    }

    // 3. Fallback con pageType: home
    if (!resp.ok() || resp.body.empty() || resp.body.find("amzn1.luna.product.") == std::string::npos) {
        pagePayload = "{\"pageContext\":{\"pageId\":\"default\",\"pageType\":\"home\",\"productStage\":\"Release\"}}";
        resp = luna::net::HttpClient::post(pageUrl, pagePayload, headers);
        std::printf("[LunaCatalog] getPage(pageType: home) -> HTTP %d (tam: %zu)\n", resp.statusCode, resp.body.size());
    }

    // 4. Fallback con pageType: multistate_navigation
    if (!resp.ok() || resp.body.empty() || resp.body.find("amzn1.luna.product.") == std::string::npos) {
        pagePayload = "{\"pageContext\":{\"pageId\":\"default\",\"pageType\":\"multistate_navigation\"}}";
        resp = luna::net::HttpClient::post(pageUrl, pagePayload, headers);
        std::printf("[LunaCatalog] getPage(multistate_navigation) -> HTTP %d (tam: %zu)\n", resp.statusCode, resp.body.size());
    }

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
        const std::string needle = "amzn1.luna.product.";

        while (result.size() < 12 && (searchPos = body.find(needle, searchPos)) != std::string::npos) {
            size_t idEnd = body.find_first_of("\"' \t\r\n,}", searchPos);
            if (idEnd == std::string::npos) break;
            std::string gameId = body.substr(searchPos, idEnd - searchPos);

            bool dup = false;
            for (const auto& item : result) {
                if (item.id == gameId) { dup = true; break; }
            }
            if (dup) {
                searchPos = idEnd;
                continue;
            }

            size_t winStart = (searchPos > 2000) ? (searchPos - 2000) : 0;
            size_t winEnd = std::min(body.size(), searchPos + 2000);
            std::string window = body.substr(winStart, winEnd - winStart);

            std::string title = extractJsonString(window, "title");
            if (title.empty()) title = extractJsonString(window, "productTitle");
            if (title.empty()) title = extractJsonString(window, "gameTitle");
            if (title.empty()) title = extractJsonString(window, "name");

            std::string publisher = extractJsonString(window, "publisher");
            if (publisher.empty()) publisher = extractJsonString(window, "developer");
            if (publisher.empty()) publisher = extractJsonString(window, "publisherName");

            std::string synopsis = extractJsonString(window, "synopsis");
            if (synopsis.empty()) synopsis = extractJsonString(window, "shortDescription");
            if (synopsis.empty()) synopsis = extractJsonString(window, "description");

            std::string category = extractJsonString(window, "badgeText");
            if (category.empty()) category = extractJsonString(window, "subscription");
            if (category.empty()) category = extractJsonString(window, "category");

            std::string coverUrl = extractJsonString(window, "heroImageUrl");
            if (coverUrl.empty()) coverUrl = extractJsonString(window, "thumbnailUrl");
            if (coverUrl.empty()) coverUrl = extractJsonString(window, "boxArtUrl");
            if (coverUrl.empty()) coverUrl = extractJsonString(window, "imageUrl");
            if (coverUrl.empty()) coverUrl = extractJsonString(window, "squareImageUrl");

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
                    luna::util::CrashLogger::log("[LunaCatalog] Descargando portada para '%s' (%s)...",
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
            m_statusMessage += " [Sesion caducada o perfil no activo]";
        } else if (resp.statusCode == 401) {
            m_statusMessage += " [Token no autorizado]";
        } else if (resp.statusCode == 400) {
            m_statusMessage += " [Peticion invalida]";
        }
    } else {
        m_statusMessage = "Error de red al conectar con Amazon Luna";
    }

    luna::util::CrashLogger::log("[LunaCatalog] Total de juegos dinamicos cargados: %zu - %s",
                                 result.size(), m_statusMessage.c_str());
    return result;
}

bool LunaSessionClient::startSession(const std::string& gameId,
                                    const std::string& accessToken,
                                    const std::string& sdpOffer,
                                    const std::string& deviceSerial,
                                    const std::string& customerId) {
    m_info = GameSessionInfo{};
    m_info.gameId = gameId;
    m_accessToken = accessToken;
    m_pollAttempts = 0;
    m_lastPollTime = std::chrono::steady_clock::now();
    m_state = SessionState::CreatingJob;
    m_statusMessage = "Solicitando sesion de juego a Amazon Tempo...";

    luna::util::CrashLogger::logStep(4, "createGameSessionJob sent to Amazon Tempo...");

    m_proxyHost = "https://proxy-prod.eu-west-1.tempo.digital.a2z.com";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"x-amz-access-token", accessToken},
        {"x-amz-platform", "smart_tv_web"},
        {"x-amz-device-type", "A2U9WJDSJD7QG9"},
        {"x-amz-device-model", "LG TV"},
        {"x-amz-client-version", "1.0.37063.0"},
        {"x-amz-react-version", "1.0.37063.0"},
        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},
        {"x-amz-country-of-residence", "ES"},
        {"x-amz-timezone", "Europe/Madrid"},
        {"x-amz-locale", "es-ES"},
        {"Accept-Language", "es-ES"}
    };

    if (!deviceSerial.empty()) {
        headers.push_back({"x-amz-device-serial-number", deviceSerial});
    }

    // Paso A: Resolver token de perfil Luna y TDI
    resolveProfileToken(headers, customerId);

    if (!m_profileToken.empty()) {
        headers.push_back({"x-amz-profile-token", m_profileToken});
    }

    // Paso B: Resolver playToken especifico del juego en el catalogo
    std::string playToken = resolvePlayToken(gameId, headers);

    // Paso C: Construir oferta SDP y payload createGameSessionJob
    std::string signalingInner = "{\"type\":\"offer\",\"sdp\":\"" + escapeJson(sdpOffer) + "\","
        "\"extras\":{\"features\":[],\"requestedChannels\":[\"display\",\"audio\",\"input\",\"participant\"],"
        "\"clientContext\":{\"browserClientRole\":\"BROWSER\",\"browserType\":\"Chrome\",\"browserVersion\":\"120.0.0.0\","
        "\"deviceModel\":\"LG TV\",\"deviceType\":\"smarttv\",\"osName\":\"webOS\",\"osVersion\":\"9.0\"}}}";

    std::string payload = "{\n"
        "  \"gameId\": \"" + gameId + "\",\n";
    if (!playToken.empty()) {
        payload += "  \"playToken\": \"" + playToken + "\",\n";
    }
    payload +=
        "  \"productStage\": \"Release\",\n"
        "  \"signalingRequest\": \"" + escapeJson(signalingInner) + "\",\n"
        "  \"timeout\": 15000,\n"
        "  \"clientFeatures\": {\n"
        "    \"h264\": true,\n"
        "    \"opus\": true,\n"
        "    \"dataChannels\": true\n"
        "  },\n"
        "  \"clientContext\": {\n"
        "    \"clientType\": \"BROWSER\",\n"
        "    \"deviceType\": \"SMART_TV\",\n"
        "    \"deviceModel\": \"LG TV\"\n"
        "  }\n"
        "}";

    std::string url = m_proxyHost + "/createGameSessionJob";
    auto resp = luna::net::HttpClient::post(url, payload, headers);
    std::printf("[LunaSession] createGameSessionJob EU -> HTTP %d: %s\n",
                resp.statusCode, resp.body.c_str());

    // Si devuelve 499 o error de gateway, probar fallback Web con cabeceras completas
    if (!resp.ok() && (resp.statusCode == 499 || resp.statusCode == 400 || resp.statusCode == 500)) {
        std::printf("[LunaSession] HTTP %d recibido. Probando con perfil Web directo...\n", resp.statusCode);
        std::vector<std::pair<std::string, std::string>> webHeaders = {
            {"Content-Type", "application/json"},
            {"x-amz-access-token", accessToken},
            {"x-amz-platform", "web"},
            {"x-amz-device-type", "A1PY8QQU9P0WJV"},
            {"x-amz-device-model", "Chrome/120.0.0.0"},
            {"x-amz-client-version", "1.0.37063.0"},
            {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},
            {"x-amz-country-of-residence", "ES"},
            {"x-amz-locale", "es-ES"},
            {"Accept-Language", "es-ES"}
        };
        if (!deviceSerial.empty()) {
            webHeaders.push_back({"x-amz-device-serial-number", deviceSerial});
        }
        if (!m_profileToken.empty()) {
            webHeaders.push_back({"x-amz-profile-token", m_profileToken});
        }
        for (const auto& h : headers) {
            if (h.first == "x-amz-tdi") webHeaders.push_back(h);
        }
        resp = luna::net::HttpClient::post(url, payload, webHeaders);
        std::printf("[LunaSession] createGameSessionJob Web fallback -> HTTP %d: %s\n",
                    resp.statusCode, resp.body.c_str());
    }

    // Fallback a endpoint global si regional falla
    if (!resp.ok() && (resp.statusCode == 404 || resp.statusCode == 499 || resp.statusCode == 500 || resp.statusCode == 0)) {
        std::printf("[LunaSession] Fallo regional EU (HTTP %d). Probando endpoint global...\n", resp.statusCode);
        m_proxyHost = "https://proxy-prod.tempo.digital.a2z.com";
        url = m_proxyHost + "/createGameSessionJob";
        resp = luna::net::HttpClient::post(url, payload, headers);
    }

    if (!resp.ok()) {
        m_state = SessionState::Failed;
        std::string err = "Error Amazon (HTTP " + std::to_string(resp.statusCode) + ")";
        if (resp.statusCode == 403) {
            err = "Sesion Amazon caducada (403). Pulsa [TRIANGULO] para revincular.";
        } else if (resp.statusCode == 499) {
            err = std::string("Amazon 499: perfil no autorizado (") + (m_profileToken.empty() ? "sin profileToken" : "token rechazado") + ")";
        } else if (!resp.body.empty() && resp.body != "{}") {
            err += ": " + resp.body.substr(0, 90);
        } else if (!resp.error.empty()) {
            err += ": " + resp.error.substr(0, 90);
        }
        m_statusMessage = err;
        std::printf("[LunaSession] createGameSessionJob falló con HTTP %d: %s | %s\n",
                    resp.statusCode, resp.error.c_str(), resp.body.c_str());
        return false;
    }

    m_info.jobId = extractJsonString(resp.body, "gameSessionJobId");
    if (m_info.jobId.empty()) {
        m_state = SessionState::Failed;
        m_statusMessage = "No se recibio gameSessionJobId de Amazon";
        return false;
    }

    m_state = SessionState::PollingJob;
    m_statusMessage = "Esperando asignacion de servidor Amazon...";
    std::printf("[LunaSession] Job creado: %s, comenzando sondeo...\n", m_info.jobId.c_str());
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
    std::string payload = "{\n"
        "  \"gameSessionJobId\": \"" + m_info.jobId + "\"\n"
        "}";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"x-amz-access-token", m_accessToken},
        {"x-amz-platform", "smart_tv_web"},
        {"x-amz-device-type", "A2U9WJDSJD7QG9"},
        {"x-amz-marketplace-id", "A1RKKUPIHCS9HS"},
        {"x-amz-country-of-residence", "ES"},
        {"x-amz-locale", "es-ES"},
        {"Accept-Language", "es-ES"}
    };
    if (!m_profileToken.empty()) {
        headers.push_back({"x-amz-profile-token", m_profileToken});
    }

    auto resp = luna::net::HttpClient::post(url, payload, headers);
    if (!resp.ok()) {
        return false;
    }

    bool hasPublicIp = (resp.body.find("\"publicIp\"") != std::string::npos);
    bool isCompleted = (resp.body.find("\"COMPLETED_SUCCEEDED\"") != std::string::npos);

    if (isCompleted || hasPublicIp) {
        m_info.publicIp = extractJsonString(resp.body, "publicIp");
        m_info.sessionId = extractJsonString(resp.body, "sessionId");

        // Extraer SDP Answer de signalingResponse
        std::string rawSig = extractJsonString(resp.body, "signalingResponse");
        if (!rawSig.empty()) {
            std::string unescaped = unescapeJson(rawSig);
            m_info.sdpAnswer = extractJsonString(unescaped, "sdp");
        }

        if (m_info.sdpAnswer.empty()) {
            m_info.sdpAnswer = extractJsonString(resp.body, "sdp");
        }

        // Buscar puerto en la respuesta o en el SDP (m=audio <port>)
        if (!m_info.sdpAnswer.empty()) {
            size_t mPos = m_info.sdpAnswer.find("m=audio ");
            if (mPos != std::string::npos) {
                int p = 0;
                if (std::sscanf(m_info.sdpAnswer.c_str() + mPos + 8, "%d", &p) == 1 && p > 0) {
                    m_info.port = p;
                }
            }

            size_t ufragPos = m_info.sdpAnswer.find("a=ice-ufrag:");
            if (ufragPos != std::string::npos) {
                size_t e = m_info.sdpAnswer.find_first_of("\r\n", ufragPos);
                m_info.iceUfrag = m_info.sdpAnswer.substr(ufragPos + 12, e - (ufragPos + 12));
            }

            size_t pwdPos = m_info.sdpAnswer.find("a=ice-pwd:");
            if (pwdPos != std::string::npos) {
                size_t e = m_info.sdpAnswer.find_first_of("\r\n", pwdPos);
                m_info.icePwd = m_info.sdpAnswer.substr(pwdPos + 10, e - (pwdPos + 10));
            }
        }

        if (m_info.port == 0) m_info.port = 33438; // Puerto por defecto Tempo

        if (!m_info.publicIp.empty()) {
            m_state = SessionState::Ready;
            m_statusMessage = "¡Sesion asignada! Conectando a " + m_info.publicIp + ":" + std::to_string(m_info.port);
            luna::util::CrashLogger::logStep(5, "Polling getGameSessionJob completed successfully...");
            luna::util::CrashLogger::log("[LunaSession] Asignada con exito: IP=%s:%d, SessionId=%s",
                                         m_info.publicIp.c_str(), m_info.port, m_info.sessionId.c_str());
            return true;
        }
    } else if (resp.body.find("\"COMPLETED_FAILED\"") != std::string::npos) {
        m_state = SessionState::Failed;
        std::string errCode = extractJsonString(resp.body, "errorCode");
        m_statusMessage = "Rechazado por Amazon Tempo: " + (errCode.empty() ? "COMPLETED_FAILED" : errCode);
        std::printf("[LunaSession] Job fallo: %s\n", resp.body.c_str());
        return false;
    }

    return false;
}

void LunaSessionClient::terminateSession(const std::string& accessToken) {
    if (m_info.sessionId.empty()) return;

    std::string url = m_proxyHost + "/terminateGameSession";
    std::string payload = "{\n"
        "  \"sessionId\": \"" + m_info.sessionId + "\"\n"
        "}";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"x-amz-access-token", accessToken},
        {"x-amz-platform", "smart_tv_web"},
        {"x-amz-device-type", "A1PY8QQU9P0WJV"}
    };

    luna::net::HttpClient::post(url, payload, headers);
    m_state = SessionState::Terminated;
    m_statusMessage = "Sesion de juego finalizada";
    std::printf("[LunaSession] Sesion terminada en Amazon\n");
}

} // namespace luna::session
