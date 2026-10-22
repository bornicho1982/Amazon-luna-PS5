#include "session/luna_session.h"
#include "net/http_client.h"

#include <cstdio>

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
    // 1. Consultar pagina provision_game
    std::string pageUrl = m_proxyHost + "/getPage";
    std::string payload1 = "{\"pageContext\":{\"pageId\":\"" + gameId + "\",\"pageType\":\"provision_game\",\"productStage\":\"Release\"}}";
    auto resp1 = luna::net::HttpClient::post(pageUrl, payload1, headers);
    std::printf("[LunaSession] getPage(provision_game, %s) -> HTTP %d (tam: %zu)\n",
                gameId.c_str(), resp1.statusCode, resp1.body.size());

    if (resp1.ok()) {
        std::string tok = extractJsonString(resp1.body, "playToken");
        if (!tok.empty()) {
            std::printf("[LunaSession] playToken detectado en provision_game!\n");
            return tok;
        }
    }

    // 2. Fallback: Consultar pagina multistate_detail
    std::string payload2 = "{\"pageContext\":{\"pageId\":\"" + gameId + "\",\"pageType\":\"multistate_detail\"}}";
    auto resp2 = luna::net::HttpClient::post(pageUrl, payload2, headers);
    std::printf("[LunaSession] getPage(multistate_detail, %s) -> HTTP %d (tam: %zu)\n",
                gameId.c_str(), resp2.statusCode, resp2.body.size());

    if (resp2.ok()) {
        std::string tok = extractJsonString(resp2.body, "playToken");
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

    std::string profileId;

    // 2. Solicitar pagina de navegacion inicial para identificar perfil
    std::string pageUrl = m_proxyHost + "/getPage";
    std::string pagePayload = "{\"pageContext\":{\"pageId\":\"default\",\"pageType\":\"multistate_navigation\"}}";
    auto pageResp = luna::net::HttpClient::post(pageUrl, pagePayload, headers);
    std::printf("[LunaSession] getPage(multistate_navigation) -> HTTP %d (tam: %zu)\n",
                pageResp.statusCode, pageResp.body.size());

    if (pageResp.ok()) {
        profileId = extractJsonString(pageResp.body, "profileId");
        if (profileId.empty()) profileId = extractJsonString(pageResp.body, "directedProfileId");
        if (profileId.empty()) profileId = extractJsonString(pageResp.body, "directedId");
        if (profileId.empty()) profileId = extractJsonString(pageResp.body, "ownerProfileId");
        if (profileId.empty()) profileId = extractJsonString(pageResp.body, "defaultProfileId");
        if (!profileId.empty()) {
            std::printf("[LunaSession] profileId encontrado en catalogo: %s\n", profileId.c_str());
        }
    }

    // 3. Si no vino en getPage, usar customerId de la cuenta activa
    if (profileId.empty() && !customerId.empty()) {
        profileId = customerId;
        std::printf("[LunaSession] Usando customerId como profileId: %s\n", profileId.c_str());
    }

    // 4. Con profileId obtenido, realizar playerLogin para obtener x-amz-profile-token
    std::string loginUrl = m_proxyHost + "/player/playerLogin";
    if (!profileId.empty()) {
        std::string loginPayload = "{\"profileId\":\"" + profileId + "\"}";
        auto lResp = luna::net::HttpClient::post(loginUrl, loginPayload, headers);
        std::printf("[LunaSession] playerLogin(%s) -> HTTP %d, body: %s\n",
                    profileId.c_str(), lResp.statusCode, lResp.body.c_str());
        if (lResp.ok()) {
            std::string tok = extractJsonString(lResp.body, "token");
            if (!tok.empty()) {
                m_profileToken = tok;
                std::printf("[LunaSession] Token de perfil Luna (x-amz-profile-token) obtenido con exito!\n");
                return m_profileToken;
            }
        }
    }

    // 5. Fallback: playerLogin con payload vacio para perfil por defecto
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

    return "";
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
            std::printf("[LunaSession] Asignada con exito: IP=%s:%d, SessionId=%s\n",
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
