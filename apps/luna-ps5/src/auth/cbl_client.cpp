#include "auth/cbl_client.h"
#include "net/http_client.h"
#include "util/qrcodegen.h"
#include "util/crash_logger.h"

#include <cstdio>
#include <ctime>
#include <chrono>
#include <cstdint>

namespace luna::auth {

namespace {

std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";

    pos = json.find('"', pos);
    if (pos == std::string::npos) return "";

    size_t endPos = json.find('"', pos + 1);
    if (endPos == std::string::npos) return "";

    return json.substr(pos + 1, endPos - pos - 1);
}

int extractJsonInt(const std::string& json, const std::string& key, int defaultVal) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;

    pos = json.find(':', pos);
    if (pos == std::string::npos) return defaultVal;

    pos = json.find_first_not_of(" \t\r\n\"", pos + 1);
    if (pos == std::string::npos) return defaultVal;

    size_t endPos = json.find_first_of(",}\" \t\r\n", pos);
    if (endPos == std::string::npos) endPos = json.length();

    char* endPtr = nullptr;
    long val = std::strtol(json.c_str() + pos, &endPtr, 10);
    if (endPtr == json.c_str() + pos) return defaultVal;
    return static_cast<int>(val);
}

} // namespace

CblClient::CblClient() {
    m_qrCode.resize(qrcodegen_BUFFER_LEN_MAX, 0);
}

CblClient::~CblClient() = default;

std::string CblClient::getOrGenerateDeviceSerial() {
    std::string path = m_storageDir.empty() ? "device_id.txt" : m_storageDir + "/device_id.txt";
    FILE* f = std::fopen(path.c_str(), "r");
    if (f) {
        char buf[64] = {0};
        if (std::fgets(buf, sizeof(buf), f)) {
            std::fclose(f);
            std::string s(buf);
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
                s.pop_back();
            }
            if (s.length() == 32) {
                return s;
            }
        } else {
            std::fclose(f);
        }
    }

    auto getEntropy = [](uint64_t extra) -> uint64_t {
        uint64_t tsc = 0;
#if defined(__x86_64__) || defined(_M_X64)
        tsc = __builtin_ia32_rdtsc();
#endif
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        uint64_t val = static_cast<uint64_t>(now) ^ (tsc * 6364136223846793005ULL + 1442695040888963407ULL + extra);
        val ^= val >> 30;
        val *= 0xbf58476d1ce4e5b9ULL;
        val ^= val >> 27;
        val *= 0x94d049bb133111ebULL;
        val ^= val >> 31;
        return val;
    };

    uint64_t part1 = getEntropy(0x1337c0deULL);
    uint64_t part2 = getEntropy(0xdeadbeefULL);

    char hexBuf[33];
    std::snprintf(hexBuf, sizeof(hexBuf), "%016llx%016llx",
                  static_cast<unsigned long long>(part1),
                  static_cast<unsigned long long>(part2));
    std::string newSerial(hexBuf);

    f = std::fopen(path.c_str(), "w");
    if (f) {
        std::fputs(newSerial.c_str(), f);
        std::fclose(f);
        std::printf("[CBL] Nuevo device_serial persistente guardado en %s: %s\n", path.c_str(), newSerial.c_str());
    }
    return newSerial;
}

bool CblClient::init(const std::string& storageDir) {
    m_storageDir = storageDir;
    m_storagePath = storageDir.empty() ? "account.json" : storageDir + "/account.json";
    m_deviceSerial = getOrGenerateDeviceSerial();
    m_state = CblState::CheckingStorage;

    generateQrCode();

    if (loadAccountFile()) {
        if (!m_tokens.accessToken.empty()) {
            int64_t now = static_cast<int64_t>(std::time(nullptr));
            if (m_tokens.expiresAt > (now + 60)) {
                m_state = CblState::Authenticated;
                std::printf("[CBL] Sesion activa cargada desde %s (expira en %lld s)\n",
                            m_storagePath.c_str(), static_cast<long long>(m_tokens.expiresAt - now));
                return true;
            }
        }
        if (!m_tokens.refreshToken.empty()) {
            std::printf("[CBL] Token posiblemente caducado, refrescando con refresh_token...\n");
            if (refreshAccessToken()) {
                m_state = CblState::Authenticated;
                std::printf("[CBL] Sesion refrescada con exito desde %s\n", m_storagePath.c_str());
                return true;
            }
        }
        std::printf("[CBL] Sesion previa no valida ni renovable. Eliminando %s\n", m_storagePath.c_str());
        std::remove(m_storagePath.c_str());
    }

    return requestCblCodePair();
}

void CblClient::tick() {
    if (m_state == CblState::Error) {
        auto now = std::chrono::steady_clock::now();
        int errElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPollTime).count();
        if (errElapsed >= 4) {
            m_lastPollTime = now;
            requestCblCodePair();
        }
        return;
    }

    if (m_state != CblState::WaitingForUser) {
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // Check code expiry
    int elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_codeAcquiredTime).count();
    if (elapsed >= m_expiresInSec) {
        std::printf("[CBL] Codigo expirado, solicitando uno nuevo...\n");
        requestCblCodePair();
        return;
    }

    // Poll according to interval
    int pollElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastPollTime).count();
    if (pollElapsed >= m_pollingIntervalSec) {
        m_lastPollTime = now;
        pollRegistration();
    }
}

void CblClient::logout() {
    m_tokens = AccountTokens{};
    m_state = CblState::Uninitialized;
    std::remove(m_storagePath.c_str());
    requestCblCodePair();
}

bool CblClient::getQrModule(int x, int y) const {
    if (m_qrSize <= 0 || m_qrCode.empty()) return false;
    return qrcodegen_getModule(m_qrCode.data(), x, y);
}

bool CblClient::refreshAccessToken() {
    if (m_tokens.refreshToken.empty()) {
        std::printf("[CBL] No hay refresh_token disponible para refrescar\n");
        return false;
    }

    int64_t now = static_cast<int64_t>(std::time(nullptr));
    if (m_tokens.expiresAt > (now + 300) && !m_tokens.accessToken.empty()) {
        std::printf("[CBL] El token actual todavia es valido durante %lld s, omitiendo refresco\n",
                    static_cast<long long>(m_tokens.expiresAt - now));
        return true;
    }

    std::printf("[CBL] Refrescando access_token en api.amazon.com/auth/token...\n");
    std::string url = "https://api.amazon.com/auth/token";
    std::string payload = "{\n"
        "  \"app_name\": \"Amazon Luna\",\n"
        "  \"app_version\": \"1.0.37063.0\",\n"
        "  \"source_token_type\": \"refresh_token\",\n"
        "  \"source_token\": \"" + m_tokens.refreshToken + "\",\n"
        "  \"requested_token_type\": \"access_token\"\n"
        "}";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"Accept-Language", "en_US"}
    };

    auto resp = luna::net::HttpClient::post(url, payload, headers);
    if (!resp.ok()) {
        luna::util::CrashLogger::log("[CBL] Error al refrescar token: HTTP %d %s: %.150s",
                                     resp.statusCode, resp.error.c_str(), resp.body.c_str());
        std::printf("[CBL] Error al refrescar token: HTTP %d %s: %s\n",
                    resp.statusCode, resp.error.c_str(), resp.body.c_str());
        return false;
    }

    std::string newToken = extractJsonString(resp.body, "access_token");
    if (newToken.empty()) {
        newToken = extractJsonString(resp.body, "accessToken");
    }

    if (newToken.empty()) {
        std::printf("[CBL] Token refrescado no tiene access_token: %s\n", resp.body.c_str());
        return false;
    }

    m_tokens.accessToken = newToken;
    std::string newRefresh = extractJsonString(resp.body, "refresh_token");
    if (!newRefresh.empty()) {
        m_tokens.refreshToken = newRefresh;
    }
    int expiresIn = extractJsonInt(resp.body, "expires_in", 3600);
    m_tokens.expiresAt = static_cast<int64_t>(std::time(nullptr)) + expiresIn;

    saveAccountFile();
    std::printf("[CBL] Access token refrescado con exito (expira en %d s)\n", expiresIn);
    return true;
}

bool CblClient::loadAccountFile() {
    FILE* f = std::fopen(m_storagePath.c_str(), "rb");
    if (!f) return false;

    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 65536) {
        std::fclose(f);
        return false;
    }

    std::string json(static_cast<size_t>(size), '\0');
    size_t readBytes = std::fread(&json[0], 1, static_cast<size_t>(size), f);
    std::fclose(f);

    if (readBytes == 0) return false;

    m_tokens.accessToken = extractJsonString(json, "access_token");
    m_tokens.refreshToken = extractJsonString(json, "refresh_token");
    m_tokens.customerId = extractJsonString(json, "customer_id");
    m_tokens.expiresAt = extractJsonInt(json, "expires_at", 0);

    if (m_tokens.accessToken.empty() && m_tokens.refreshToken.empty()) {
        return false;
    }

    // Si disponemos de refresh_token, obtener un token reciente y fresco al iniciar
    if (!m_tokens.refreshToken.empty()) {
        refreshAccessToken();
    }

    return !m_tokens.accessToken.empty();
}

bool CblClient::saveAccountFile() {
    FILE* f = std::fopen(m_storagePath.c_str(), "wb");
    if (!f) return false;

    std::string json = "{\n";
    json += "  \"access_token\": \"" + m_tokens.accessToken + "\",\n";
    json += "  \"refresh_token\": \"" + m_tokens.refreshToken + "\",\n";
    json += "  \"customer_id\": \"" + m_tokens.customerId + "\",\n";
    json += "  \"expires_at\": " + std::to_string(m_tokens.expiresAt) + "\n";
    json += "}\n";

    std::fwrite(json.data(), 1, json.size(), f);
    std::fclose(f);
    std::printf("[CBL] Cuenta guardada con exito en %s (%zu bytes)\n", m_storagePath.c_str(), json.size());
    return true;
}

bool CblClient::requestCblCodePair() {
    m_state = CblState::RequestingCode;
    m_errorMessage.clear();

    if (m_deviceSerial.empty()) {
        m_deviceSerial = getOrGenerateDeviceSerial();
    }

    std::string url = "https://api.amazon.com/auth/create/codepair";
    std::string payload = "{\n"
        "  \"code_data\": {\n"
        "    \"domain\": \"Device\",\n"
        "    \"device_name\": \"%FIRST_NAME%'s%DUPE_STRATEGY_1ST% LG TV\",\n"
        "    \"app_name\": \"Amazon Luna\",\n"
        "    \"app_version\": \"1.0.37063.0\",\n"
        "    \"device_model\": \"LG TV\",\n"
        "    \"device_serial\": \"" + m_deviceSerial + "\",\n"
        "    \"device_type\": \"A2U9WJDSJD7QG9\",\n"
        "    \"os_version\": \"999\"\n"
        "  }\n"
        "}";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"Accept-Language", "en_US"}
    };

    auto resp = luna::net::HttpClient::post(url, payload, headers);
    if (!resp.ok()) {
        m_state = CblState::Error;
        m_errorMessage = "Error solicitando codigo Amazon: " + resp.error;
        std::printf("[CBL] Error al solicitar codepair: %s\n", resp.error.c_str());
        return false;
    }

    m_publicCode = extractJsonString(resp.body, "public_code");
    m_privateCode = extractJsonString(resp.body, "private_code");
    m_pollingIntervalSec = extractJsonInt(resp.body, "polling_interval_in_seconds", 3);
    m_expiresInSec = extractJsonInt(resp.body, "expires_in", 600);

    if (m_publicCode.empty() || m_privateCode.empty()) {
        m_state = CblState::Error;
        m_errorMessage = "Respuesta incompleta de Amazon";
        return false;
    }

    m_codeAcquiredTime = std::chrono::steady_clock::now();
    m_lastPollTime = m_codeAcquiredTime;
    m_state = CblState::WaitingForUser;

    generateQrCode();
    std::printf("[CBL] Codigo generado: %s (expira en %d s, sondeo cada %d s)\n",
                m_publicCode.c_str(), m_expiresInSec, m_pollingIntervalSec);
    return true;
}

bool CblClient::pollRegistration() {
    if (m_deviceSerial.empty()) {
        m_deviceSerial = getOrGenerateDeviceSerial();
    }

    std::string url = "https://api.amazon.com/auth/register";
    std::string payload = "{\n"
        "  \"auth_data\": {\n"
        "    \"code_pair\": {\n"
        "      \"public_code\": \"" + m_publicCode + "\",\n"
        "      \"private_code\": \"" + m_privateCode + "\"\n"
        "    }\n"
        "  },\n"
        "  \"registration_data\": {\n"
        "    \"domain\": \"Device\",\n"
        "    \"device_name\": \"%FIRST_NAME%'s%DUPE_STRATEGY_1ST% LG TV\",\n"
        "    \"app_name\": \"Amazon Luna\",\n"
        "    \"app_version\": \"1.0.37063.0\",\n"
        "    \"device_model\": \"LG TV\",\n"
        "    \"device_serial\": \"" + m_deviceSerial + "\",\n"
        "    \"device_type\": \"A2U9WJDSJD7QG9\",\n"
        "    \"os_version\": \"999\"\n"
        "  },\n"
        "  \"requested_token_type\": [\"bearer\"]\n"
        "}";

    std::vector<std::pair<std::string, std::string>> headers = {
        {"Content-Type", "application/json"},
        {"Accept-Language", "en_US"}
    };

    auto resp = luna::net::HttpClient::post(url, payload, headers);
    std::printf("[CBL] Sondeo: status=%d, error=%s, body_len=%zu\n",
                resp.statusCode, resp.error.c_str(), resp.body.size());

    if (resp.statusCode == 401) {
        m_errorMessage = "Esperando que confirmes en tu movil (HTTP 401)...";
        return false;
    }

    if (!resp.ok()) {
        std::string err = "Error sondeo Amazon (HTTP " + std::to_string(resp.statusCode) + ")";
        if (!resp.error.empty()) err += ": " + resp.error;
        if (!resp.body.empty()) err += " | " + resp.body.substr(0, 70);
        m_errorMessage = err;
        return false;
    }

    std::string token = extractJsonString(resp.body, "access_token");
    if (token.empty()) token = extractJsonString(resp.body, "accessToken");
    if (token.empty()) token = extractJsonString(resp.body, "token");

    std::string refresh = extractJsonString(resp.body, "refresh_token");
    if (refresh.empty()) refresh = extractJsonString(resp.body, "refreshToken");

    std::string customer = extractJsonString(resp.body, "customer_id");
    if (customer.empty()) customer = extractJsonString(resp.body, "customerId");
    if (customer.empty()) customer = extractJsonString(resp.body, "directed_id");
    if (customer.empty()) customer = extractJsonString(resp.body, "user_id");

    if (token.empty()) {
        m_errorMessage = "Amazon aprobo (200 OK) pero formato inesperado: " + resp.body.substr(0, 70);
        std::printf("[CBL] 200 OK sin access_token. Body: %s\n", resp.body.c_str());
        return false;
    }

    m_tokens.accessToken = token;
    m_tokens.refreshToken = refresh;
    m_tokens.customerId = customer.empty() ? "AmazonUser" : customer;
    int expiresIn = extractJsonInt(resp.body, "expires_in", 3600);
    m_tokens.expiresAt = static_cast<int64_t>(std::time(nullptr)) + expiresIn;

    m_state = CblState::Authenticated;
    saveAccountFile();
    m_errorMessage = "¡Vinculacion completada con exito!";
    std::printf("[CBL] ¡Vinculacion completada con exito! Guardado en %s\n", m_storagePath.c_str());
    return true;
}

void CblClient::generateQrCode() {
    std::string qrText = "https://amazon.com/code";
    std::vector<uint8_t> tempBuffer(qrcodegen_BUFFER_LEN_MAX, 0);

    bool ok = qrcodegen_encodeText(qrText.c_str(),
                                   tempBuffer.data(),
                                   m_qrCode.data(),
                                   qrcodegen_Ecc_LOW,
                                   1, 40,
                                   qrcodegen_Mask_AUTO,
                                   true);
    if (ok) {
        m_qrSize = qrcodegen_getSize(m_qrCode.data());
    } else {
        m_qrSize = 0;
    }
}

} // namespace luna::auth
