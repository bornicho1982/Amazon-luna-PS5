import os

new_auth_server_content = r'''#include "auth/auth_server.h"
#include "session/luna_session.h"
#include "display/display_manager.h"
#include "display/hero_banner_asset.h"
#include "net/http_client.h"
#include "util/crash_logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <algorithm>
#include <sstream>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CLOSE_SOCKET(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define CLOSE_SOCKET(s) ::close(s)
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
typedef int SOCKET;
#endif

#if defined(LUNA_TARGET_PS5)
extern "C" {
int sceNetInit(void);
int sceNetPoolCreate(const char* name, int size, int flags);
int* sceNetErrnoLoc(void);
int sceNetSocket(const char* name, int family, int type, int protocol);
int sceNetSetsockopt(int s, int level, int optname, const void* optval, socklen_t optlen);
int sceNetBind(int s, const struct sockaddr* addr, socklen_t addrlen);
int sceNetListen(int s, int backlog);
int sceNetAccept(int s, struct sockaddr* addr, socklen_t* addrlen);
int sceNetRecv(int s, void* buf, size_t len, int flags);
int sceNetSend(int s, const void* msg, size_t len, int flags);
int sceNetSocketAbort(int s, int flags);
int sceNetSocketClose(int s);
}
#endif

namespace luna::auth {

namespace {

std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') {
            out.push_back(' ');
        } else if (in[i] == '%' && i + 2 < in.size()) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::string extractQueryParam(const std::string& url, const std::string& param) {
    std::string pattern = param + "=";
    size_t pos = url.find(pattern);
    if (pos == std::string::npos) return "";
    pos += pattern.size();
    size_t endPos = url.find('&', pos);
    if (endPos == std::string::npos) {
        endPos = url.find(' ', pos);
    }
    std::string raw = (endPos == std::string::npos) ? url.substr(pos) : url.substr(pos, endPos - pos);
    return urlDecode(raw);
}

std::string extractFormField(const std::string& body, const std::string& key) {
    std::string pattern = key + "=";
    size_t start = body.find(pattern);
    if (start == std::string::npos) return {};
    start += pattern.size();
    size_t end = body.find('&', start);
    std::string raw = (end == std::string::npos) ? body.substr(start) : body.substr(start, end - start);
    return urlDecode(raw);
}

std::string escapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 10);
    for (char c : input) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (static_cast<unsigned char>(c) < 32) {
            // drop control chars
        } else {
            out += c;
        }
    }
    return out;
}

std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = json.find('\"', pos);
    if (pos == std::string::npos) return {};
    pos++;
    std::string val;
    bool escape = false;
    for (size_t i = pos; i < json.size(); ++i) {
        char c = json[i];
        if (escape) {
            if (c == 'r') val.push_back('\r');
            else if (c == 'n') val.push_back('\n');
            else if (c == 't') val.push_back('\t');
            else val.push_back(c);
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '\"') {
            break;
        } else {
            val.push_back(c);
        }
    }
    return val;
}

bool parseLunaSession(const std::string& raw, AuthSession& s) {
    s.rawJson = raw;
    s.publicIp = extractJsonString(raw, "publicIp");
    s.sessionId = extractJsonString(raw, "sessionId");
    s.gameId = extractJsonString(raw, "gameId");

    std::string sig = extractJsonString(raw, "signalingResponse");
    if (!sig.empty()) {
        s.sdpAnswer = extractJsonString(sig, "sdp");
        if (s.sdpAnswer.empty()) {
            s.sdpAnswer = sig;
        }
    }
    if (s.sdpAnswer.empty()) {
        s.sdpAnswer = extractJsonString(raw, "sdp");
    }

    s.port = 0;
    size_t audioPos = s.sdpAnswer.find("m=audio ");
    if (audioPos != std::string::npos) {
        s.port = std::atoi(s.sdpAnswer.c_str() + audioPos + 8);
    }
    if (s.port == 0) {
        size_t candPos = s.sdpAnswer.find("typ host");
        if (candPos != std::string::npos && candPos > 20) {
            size_t p = s.sdpAnswer.rfind(' ', candPos - 2);
            if (p != std::string::npos) {
                s.port = std::atoi(s.sdpAnswer.c_str() + p + 1);
            }
        }
    }

    size_t ufragPos = s.sdpAnswer.find("a=ice-ufrag:");
    if (ufragPos != std::string::npos) {
        size_t end = s.sdpAnswer.find_first_of("\r\n", ufragPos);
        s.iceUfrag = s.sdpAnswer.substr(ufragPos + 12, end - (ufragPos + 12));
    }

    size_t pwdPos = s.sdpAnswer.find("a=ice-pwd:");
    if (pwdPos != std::string::npos) {
        size_t end = s.sdpAnswer.find_first_of("\r\n", pwdPos);
        s.icePwd = s.sdpAnswer.substr(pwdPos + 10, end - (pwdPos + 10));
    }

    s.isValid = (!s.publicIp.empty() && s.port > 0) || !s.sdpAnswer.empty();
    return s.isValid;
}

int openTcpListener(int port, std::string& statusOut) {
#if defined(LUNA_TARGET_PS5)
    int s = sceNetSocket("luna_http", AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        s = ::socket(AF_INET, SOCK_STREAM, 0);
    }
#else
    int s = static_cast<int>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
#endif
    if (s < 0) {
        statusOut = "ERROR SOCKET()";
        return -1;
    }

    int opt = 1;
#if defined(_WIN32)
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#elif defined(LUNA_TARGET_PS5)
    sceNetSetsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in serverAddr{};
#if !defined(_WIN32)
    serverAddr.sin_len = sizeof(sockaddr_in);
#endif
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));

#if defined(LUNA_TARGET_PS5)
    int brc = sceNetBind(s, reinterpret_cast<const sockaddr*>(&serverAddr), sizeof(serverAddr));
    if (brc < 0) {
        brc = ::bind(s, reinterpret_cast<const sockaddr*>(&serverAddr), sizeof(serverAddr));
    }
#else
    int brc = ::bind(s, reinterpret_cast<const sockaddr*>(&serverAddr), sizeof(serverAddr));
#endif
    if (brc < 0) {
        int err = errno;
#if defined(LUNA_TARGET_PS5)
        if (sceNetErrnoLoc()) err = *sceNetErrnoLoc();
        sceNetSocketClose(s);
#else
        CLOSE_SOCKET(s);
#endif
        statusOut = "ERROR BIND PUERTO " + std::to_string(port) + " (ERR=" + std::to_string(err) + ")";
        return -1;
    }

#if defined(LUNA_TARGET_PS5)
    int lrc = sceNetListen(s, 16);
    if (lrc < 0) {
        lrc = ::listen(s, 16);
    }
#else
    int lrc = ::listen(s, 16);
#endif
    if (lrc < 0) {
#if defined(LUNA_TARGET_PS5)
        sceNetSocketClose(s);
#else
        CLOSE_SOCKET(s);
#endif
        statusOut = "ERROR LISTEN PUERTO " + std::to_string(port);
        return -1;
    }

    statusOut = "PUERTO TCP " + std::to_string(port) + " ACTIVO";
    return s;
}

} // namespace

AuthServer::AuthServer() = default;

AuthServer::~AuthServer() {
    stop();
}

std::string AuthServer::generateHtmlPage() const {
    std::string html;
    html.reserve(2800);
    html += "<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Amazon Luna PS5 - Puente WebRTC</title>";
    html += "<style>";
    html += "body{font-family:system-ui,-apple-system,sans-serif;background:#0a0714;color:#f8fafc;padding:20px;margin:0}";
    html += ".card{background:#180d35;border:2px solid #7c3aed;border-radius:12px;max-width:580px;margin:16px auto;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,0.5)}";
    html += "h2{color:#c4b5fd;margin-top:0;font-size:24px}.btn{background:#9146FF;color:#fff;border:none;padding:14px 20px;border-radius:8px;cursor:pointer;font-weight:800;width:100%;font-size:16px;transition:0.2s;text-decoration:none;display:inline-block;text-align:center;box-sizing:border-box}";
    html += ".btn:hover{background:#a768ff}.btn-green{background:#10b981;color:#052e16}.btn-green:hover{background:#34d399}";
    html += "textarea{width:100%;box-sizing:border-box;background:#090414;border:1px solid #4c1d95;color:#a7f3d0;padding:12px;border-radius:8px;margin:8px 0 18px;font-size:13px;font-family:monospace}";
    html += ".status{padding:12px 14px;background:#1e1b4b;border-left:4px solid #10b981;border-radius:6px;margin-bottom:20px;color:#a7f3d0}";
    html += ".info{background:#2e1065;border:1px solid #6b21a8;border-radius:8px;padding:12px;margin-bottom:16px;font-size:13px;line-height:1.5;color:#e9d5ff}";
    html += "label{font-weight:600;color:#ddd6fe;display:block}";
    html += "</style></head><body>";
    html += "<div class='card'>";
    html += "<h2>Amazon Luna PS5 (PPSA99825)</h2>";
    html += "<div class='status'><strong>Conectado a tu PS5:</strong> Motor C++ listo</div>";
    html += "<p><a href='/luna' class='btn'>ABRIR INTERFAZ WEB REAL OFICIAL DE LUNA</a></p>";
    html += "<div class='info'><strong>Opcion avanzada: Inyeccion manual de sesion:</strong><br>";
    html += "1. En tu PC/movil con Luna, pulsa F12 &rarr; Red.<br>";
    html += "2. Inicia un juego y copia la respuesta de <code>getGameSessionJob</code>.<br>";
    html += "3. Pegala aqui debajo y dale a Iniciar.</div>";
    html += "<form method='POST' action='/api/session'>";
    html += "<label>Respuesta JSON de getGameSessionJob:</label>";
    html += "<textarea name='sessionJson' rows='6' placeholder='{\"gameSession\":{\"publicIp\":\"15.217.4.14\",\"signalingResponse\":...}}'></textarea>";
    html += "<button type='submit' class='btn btn-green'>INICIAR STREAM EN LA PS5</button>";
    html += "</form>";
    html += "</div></body></html>";
    return html;
}

std::string AuthServer::generateCatalogJson() const {
    std::string json = "{\"games\":[";
    if (catalog_ && !catalog_->empty()) {
        bool first = true;
        for (const auto& g : *catalog_) {
            if (!first) json += ",";
            first = false;
            json += "{";
            json += "\"id\":\"" + escapeJsonString(g.id) + "\",";
            json += "\"title\":\"" + escapeJsonString(g.title) + "\",";
            json += "\"publisher\":\"" + escapeJsonString(g.publisher) + "\",";
            json += "\"category\":\"" + escapeJsonString(g.category) + "\",";
            json += "\"coverUrl\":\"" + escapeJsonString(g.coverUrl) + "\",";
            json += "\"backdropUrl\":\"" + escapeJsonString(g.backdropUrl) + "\",";
            json += "\"synopsis\":\"" + escapeJsonString(g.synopsis) + "\",";
            json += "\"genres\":\"" + escapeJsonString(g.genres) + "\",";
            json += "\"pegiRating\":\"" + escapeJsonString(g.pegiRating) + "\",";
            json += "\"isEntitled\":" + std::string(g.isEntitled ? "true" : "false");
            json += "}";
        }
    }
    json += "]}";
    return json;
}

std::string AuthServer::generateLunaWebPage() const {
    std::string html = R"raw(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Amazon Luna - PlayStation 5</title>
<link rel="stylesheet" href="https://m.media-amazon.com/images/S/sash/TNReoYQfTL42uz7.css">
<style>
  @font-face {
    font-family: "Amazon Ember";
    font-weight: 300;
    src: url("https://m.media-amazon.com/images/G/01/tempo/ember/AmazonEmber_Lt.woff2") format("woff2");
  }
  @font-face {
    font-family: "Amazon Ember";
    font-weight: 400;
    src: url("https://m.media-amazon.com/images/G/01/tempo/ember/AmazonEmber_Rg.woff2") format("woff2");
  }
  @font-face {
    font-family: "Amazon Ember";
    font-weight: 700;
    src: url("https://m.media-amazon.com/images/G/01/tempo/ember/AmazonEmber_Bd.woff2") format("woff2");
  }
  @font-face {
    font-family: "Amazon Ember";
    font-weight: 800;
    src: url("https://m.media-amazon.com/images/G/01/tempo/ember/AmazonEmber_He.woff2") format("woff2");
  }

  :root {
    --bg-main: #0A0714;
    --bg-surface: #140F24;
    --card-bg: #1A1429;
    --card-hover: #261D3D;
    --accent-purple: #9146FF;
    --accent-purple-light: #C4B5FD;
    --accent-cyan: #02FDDB;
    --text-primary: #FFFFFF;
    --text-secondary: #94A3B8;
    --text-muted: #64748B;
    --font-primary: "Amazon Ember", "Ember", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; outline: none; }
  body {
    background-color: var(--bg-main);
    color: var(--text-primary);
    font-family: var(--font-primary);
    overflow-x: hidden;
    user-select: none;
    -webkit-user-select: none;
    -webkit-font-smoothing: antialiased;
  }
  header {
    position: sticky;
    top: 0;
    z-index: 100;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 16px 54px;
    background: rgba(10, 7, 20, 0.94);
    border-bottom: 1px solid #1E1733;
    backdrop-filter: blur(14px);
  }
  .header-left { display: flex; align-items: center; gap: 36px; }
  .logo-area { display: flex; align-items: center; gap: 10px; cursor: pointer; }
  .luna-logo-icon { width: 28px; height: 28px; display: block; }
  .logo-text { font-size: 26px; font-weight: 800; letter-spacing: -0.6px; color: #FFF; line-height: 1; }
  nav { display: flex; gap: 26px; font-size: 15px; font-weight: 700; color: var(--text-secondary); }
  nav a { color: inherit; text-decoration: none; cursor: pointer; padding: 7px 14px; border-radius: 20px; transition: 0.2s; }
  nav a.active, nav a:hover, nav a.focused { color: #FFF; background: rgba(255,255,255,0.08); }
  nav a.focused { box-shadow: 0 0 0 2px var(--accent-cyan); }
  .header-right { display: flex; align-items: center; gap: 20px; }
  .search-box {
    display: flex;
    align-items: center;
    background: #18112C;
    border: 1px solid #2C204B;
    border-radius: 20px;
    padding: 7px 16px;
    gap: 10px;
    transition: 0.2s;
  }
  .search-box input {
    background: transparent;
    border: none;
    color: #FFF;
    font-size: 14px;
    font-family: inherit;
    width: 190px;
  }
  .search-box.focused { border-color: var(--accent-cyan); box-shadow: 0 0 0 2px rgba(2,253,219,0.3); }
  .profile-badge {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 6px 16px;
    background: #18112C;
    border: 1px solid #2C204B;
    border-radius: 20px;
    font-size: 13px;
    font-weight: 700;
  }
  .profile-badge .dot {
    width: 8px; height: 8px;
    background: #10B981;
    border-radius: 50%;
    box-shadow: 0 0 8px #10B981;
  }

  /* HERO BANNER 1:1 CLAVADO AL OFICIAL DE AMAZON LUNA */
  .hero-banner {
    position: relative;
    width: 100%;
    height: 540px;
    background-size: cover;
    background-position: center top;
    background-repeat: no-repeat;
    display: flex;
    align-items: flex-end;
    padding: 44px 64px;
    background-color: #0A0714;
    background-image: url('/assets/luna_premium_hero.jpg');
    transition: background-image 0.4s ease-in-out;
  }
  .hero-gradient {
    position: absolute;
    inset: 0;
    background: linear-gradient(90deg, #0A0714 0%, rgba(10,7,20,0.85) 42%, rgba(10,7,20,0.15) 100%),
                linear-gradient(0deg, #0A0714 0%, transparent 60%);
    pointer-events: none;
  }
  .hero-content {
    position: relative;
    z-index: 10;
    max-width: 740px;
  }
  .hero-badge {
    display: inline-block;
    background: var(--accent-purple);
    color: #FFF;
    padding: 5px 14px;
    border-radius: 6px;
    font-size: 12px;
    font-weight: 800;
    margin-bottom: 14px;
    letter-spacing: 0.8px;
    text-transform: uppercase;
  }
  .hero-title { font-size: 46px; font-weight: 800; line-height: 1.12; margin-bottom: 12px; text-shadow: 0 3px 12px rgba(0,0,0,0.7); }
  .hero-desc { font-size: 16px; color: #CBD5E1; line-height: 1.5; margin-bottom: 26px; max-width: 620px; text-shadow: 0 2px 6px rgba(0,0,0,0.9); }
  .hero-actions { display: flex; gap: 16px; align-items: center; }
  .btn-play {
    background: #FFFFFF;
    color: #0A0714;
    border: none;
    padding: 14px 34px;
    border-radius: 30px;
    font-size: 16px;
    font-weight: 800;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 10px;
    box-shadow: 0 4px 18px rgba(255,255,255,0.25);
    transition: transform 0.15s, background 0.15s, box-shadow 0.15s;
    font-family: inherit;
  }
  .btn-play svg { fill: var(--accent-purple); width: 16px; height: 16px; }
  .btn-play:hover, .btn-play.focused {
    background: #F1F5F9;
    transform: scale(1.05);
    box-shadow: 0 0 0 3px #FFF, 0 8px 26px rgba(255,255,255,0.4);
  }
  .btn-info {
    background: rgba(255,255,255,0.12);
    color: #FFF;
    border: 1px solid rgba(255,255,255,0.28);
    padding: 14px 28px;
    border-radius: 30px;
    font-size: 15px;
    font-weight: 700;
    cursor: pointer;
    backdrop-filter: blur(10px);
    transition: 0.15s;
    font-family: inherit;
  }
  .btn-info:hover, .btn-info.focused {
    background: rgba(255,255,255,0.25);
    transform: scale(1.05);
    box-shadow: 0 0 0 2px #FFF;
  }

  /* SWIMLANES & HORIZONTAL ROWS */
  .swimlane { padding: 26px 64px; }
  .swimlane-title { font-size: 24px; font-weight: 800; margin-bottom: 20px; color: #FFFFFF; }
  .swimlane-row {
    display: flex;
    gap: 24px;
    overflow-x: auto;
    padding: 12px 6px 26px 6px;
    scroll-behavior: smooth;
    scrollbar-width: none;
  }
  .swimlane-row::-webkit-scrollbar { display: none; }

  /* GAME CARD & OFFICIAL RANKING NUMBERS */
  .game-card-wrapper {
    display: flex;
    align-items: flex-end;
    flex-shrink: 0;
    margin-right: 12px;
  }
  .rank-number {
    font-size: 130px;
    font-weight: 900;
    line-height: 0.75;
    color: rgba(255, 255, 255, 0.16);
    -webkit-text-stroke: 1.5px rgba(255, 255, 255, 0.25);
    text-shadow: 0 4px 12px rgba(0, 0, 0, 0.8);
    margin-right: -32px;
    z-index: 1;
    font-family: var(--font-primary);
    user-select: none;
    pointer-events: none;
    display: flex;
    align-items: flex-end;
    padding-bottom: 8px;
  }
  .game-card {
    width: 200px;
    background: var(--card-bg);
    border-radius: 12px;
    overflow: hidden;
    cursor: pointer;
    transition: transform 0.2s cubic-bezier(0.16, 1, 0.3, 1), box-shadow 0.2s cubic-bezier(0.16, 1, 0.3, 1);
    border: 2px solid transparent;
    flex-shrink: 0;
    position: relative;
    z-index: 2;
  }
  .game-card:hover, .game-card.focused {
    transform: translateY(-8px) scale(1.04);
    box-shadow: 0 0 0 3px #FFFFFF, 0 14px 32px rgba(145, 70, 255, 0.6);
    border-color: #FFFFFF;
    z-index: 10;
  }
  .card-cover {
    width: 100%;
    height: 275px;
    object-fit: cover;
    background: #1F1735;
    display: block;
    border-radius: 10px 10px 0 0;
  }
  .card-info { padding: 12px 14px; }
  .card-title { font-size: 14px; font-weight: 700; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; color: #FFF; }
  .card-sub { font-size: 12px; color: var(--text-secondary); margin-top: 4px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }

  /* MODAL FICHA DE JUEGO */
  .modal-backdrop {
    display: none;
    position: fixed;
    inset: 0;
    z-index: 200;
    background: rgba(10, 7, 20, 0.88);
    backdrop-filter: blur(14px);
    align-items: center;
    justify-content: center;
    padding: 24px;
  }
  .modal {
    background: #161026;
    border: 1px solid #38275C;
    border-radius: 22px;
    max-width: 860px;
    width: 100%;
    overflow: hidden;
    box-shadow: 0 25px 65px rgba(0,0,0,0.92);
    position: relative;
  }
  .modal-hero {
    position: relative;
    width: 100%;
    height: 330px;
    background-size: cover;
    background-position: center;
    background-color: #1F1735;
  }
  .modal-hero-gradient {
    position: absolute;
    inset: 0;
    background: linear-gradient(0deg, #161026 0%, rgba(22,16,38,0.72) 50%, rgba(22,16,38,0.18) 100%);
  }
  .modal-body { padding: 30px 40px; }
  .modal-title { font-size: 36px; font-weight: 800; margin-bottom: 10px; color: #FFF; }
  .modal-meta-row { display: flex; gap: 16px; align-items: center; font-size: 14px; color: var(--text-secondary); margin-bottom: 18px; flex-wrap: wrap; }
  .modal-pegi { background: #38275C; color: #FFF; padding: 3px 10px; border-radius: 6px; font-weight: 800; font-size: 12px; }
  .modal-synopsis { font-size: 15px; color: #CBD5E1; line-height: 1.6; margin-bottom: 28px; max-height: 140px; overflow-y: auto; }
  .modal-actions { display: flex; justify-content: space-between; align-items: center; }
  .btn-close {
    background: transparent;
    border: 1px solid #4B3A70;
    color: #CBD5E1;
    padding: 12px 30px;
    border-radius: 30px;
    font-weight: 700;
    cursor: pointer;
    transition: 0.15s;
    font-family: inherit;
  }
  .btn-close:hover, .btn-close.focused { background: rgba(255,255,255,0.12); border-color: #FFF; color: #FFF; }

  /* OVERLAY INICIANDO PARTIDA */
  .launch-overlay {
    display: none;
    position: fixed;
    inset: 0;
    z-index: 500;
    background: radial-gradient(circle at center, #1E1238 0%, #070510 100%);
    flex-direction: column;
    align-items: center;
    justify-content: center;
    text-align: center;
    padding: 40px;
  }
  .launch-spinner {
    width: 64px; height: 64px;
    border: 5px solid rgba(145, 70, 255, 0.2);
    border-top-color: var(--accent-cyan);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    margin-bottom: 24px;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
  .launch-title { font-size: 32px; font-weight: 800; margin-bottom: 14px; }
  .launch-sub { font-size: 18px; color: var(--accent-cyan); margin-bottom: 24px; font-weight: 700; }
  .launch-box {
    background: rgba(255,255,255,0.06);
    border: 1px solid rgba(255,255,255,0.15);
    border-radius: 16px;
    padding: 20px 32px;
    max-width: 640px;
    font-size: 16px;
    line-height: 1.6;
    color: #F1F5F9;
  }
  .launch-box strong { color: #FCD34D; font-size: 18px; }

  /* DUALSENSE HUD */
  .hud-hints {
    position: fixed;
    bottom: 20px;
    right: 32px;
    z-index: 90;
    display: flex;
    gap: 16px;
    background: rgba(10, 7, 20, 0.88);
    border: 1px solid #231B38;
    padding: 8px 18px;
    border-radius: 20px;
    font-size: 13px;
    color: #94A3B8;
    backdrop-filter: blur(8px);
  }
  .hud-hints span { display: inline-flex; align-items: center; gap: 6px; }
  .hud-key {
    background: #2D2248;
    color: #FFF;
    border-radius: 4px;
    padding: 2px 7px;
    font-size: 11px;
    font-weight: 800;
  }
</style>
</head>
<body>

<header>
  <div class="header-left">
    <div class="logo-area" onclick="scrollToTop()">
      <svg class="luna-logo-icon" viewBox="0 0 32 32" fill="none">
        <defs>
          <linearGradient id="lunaGrad" x1="0%" y1="0%" x2="100%" y2="100%">
            <stop offset="0%" stop-color="#02FDDB"/>
            <stop offset="100%" stop-color="#9146FF"/>
          </linearGradient>
        </defs>
        <rect x="5" y="5" width="22" height="22" rx="7" transform="rotate(45 16 16)" fill="url(#lunaGrad)"/>
        <circle cx="16" cy="16" r="4.5" fill="#0A0714"/>
      </svg>
      <span class="logo-text">luna</span>
    </div>
    <nav id="navTabs">
      <a class="active nav-item" data-tab="home">Inicio</a>
      <a class="nav-item" data-tab="library">Biblioteca</a>
      <a class="nav-item" data-tab="gamenight">GameNight</a>
    </nav>
  </div>
  <div class="header-right">
    <div class="search-box" id="searchBox">
      <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#94A3B8" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg>
      <input type="text" id="searchInput" placeholder="Buscar juegos..." oninput="onSearchInput(this.value)">
    </div>
    <div class="profile-badge">
      <div class="dot"></div>
      <span id="profileName">Bornicho #8663</span>
    </div>
  </div>
</header>

<div class="hero-banner" id="heroBanner">
  <div class="hero-gradient"></div>
  <div class="hero-content">
    <div class="hero-badge" id="heroBadge">LUNA PREMIUM</div>
    <div class="hero-title" id="heroTitle">Luna Premium</div>
    <div class="hero-desc" id="heroDesc">Más de 100 juegos. Una suscripción. Descubre grandes novedades, clásicos y favoritos familiares para jugar al instante en la nube.</div>
    <div class="hero-actions">
      <button class="btn-play focusable" id="heroPlayBtn" onclick="launchCurrentHero()">
        <svg viewBox="0 0 24 24"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
        <span>Jugar ahora en PS5</span>
      </button>
      <button class="btn-info focusable" id="heroInfoBtn" onclick="openCurrentHeroInfo()">Más información</button>
    </div>
  </div>
</div>

<div id="swimlanesContainer"></div>

<!-- Modal Detalle -->
<div class="modal-backdrop" id="gameModal" onclick="closeModal(event)">
  <div class="modal" onclick="event.stopPropagation()">
    <div class="modal-hero" id="mHero">
      <div class="modal-hero-gradient"></div>
    </div>
    <div class="modal-body">
      <div class="modal-title" id="mTitle"></div>
      <div class="modal-meta-row">
        <span class="modal-pegi" id="mPegi">PEGI 12</span>
        <span id="mPublisher">Amazon Games</span>
        <span>•</span>
        <span id="mGenres">Acción</span>
        <span>•</span>
        <span>🎮 Mando DualSense</span>
      </div>
      <div class="modal-synopsis" id="mSynopsis"></div>
      <div class="modal-actions">
        <button class="btn-close focusable" id="mCloseBtn" onclick="closeModal()">Cerrar</button>
        <button class="btn-play focusable" id="mPlayBtn">
          <svg viewBox="0 0 24 24"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
          <span>Jugar ahora en PS5</span>
        </button>
      </div>
    </div>
  </div>
</div>

<!-- Launch Overlay -->
<div class="launch-overlay" id="launchOverlay">
  <div class="launch-spinner"></div>
  <div class="launch-title" id="launchTitle">¡Iniciando juego en el motor nativo de PS5!</div>
  <div class="launch-sub" id="launchSub">Conectando WebRTC a 1080p 60 FPS...</div>
  <div class="launch-box">
    <strong>Pulsa CÍRCULO (O) para cerrar esta ventana del navegador</strong><br>
    y pasar directamente a la pantalla completa del juego con decodificación por hardware libSceVideodec2 y SceAudioOut.
  </div>
</div>

<div class="hud-hints">
  <span><span class="hud-key">X</span> Seleccionar / Jugar</span>
  <span><span class="hud-key">O</span> Volver / Salir</span>
  <span><span class="hud-key">L1/R1</span> Pestañas</span>
  <span><span class="hud-key">▲/▼/◄/►</span> Navegar</span>
</div>

<script>
let catalogGames = [];
let collectionsData = [];
let heroGame = null;
let currentModalGame = null;
let focusables = [];
let focusIndex = 0;

function scrollToTop() {
  window.scrollTo({ top: 0, behavior: 'smooth' });
}

function updateFocusables() {
  focusables = Array.from(document.querySelectorAll('.focusable, .game-card, .nav-item'));
  if (document.getElementById('gameModal').style.display === 'flex') {
    focusables = Array.from(document.querySelectorAll('#gameModal .focusable'));
  }
}

function setFocus(index) {
  updateFocusables();
  if (focusables.length === 0) return;
  if (index < 0) index = 0;
  if (index >= focusables.length) index = focusables.length - 1;
  focusIndex = index;
  focusables.forEach((el, idx) => {
    if (idx === focusIndex) {
      el.classList.add('focused');
      el.scrollIntoView({ behavior: 'smooth', block: 'nearest', inline: 'center' });
    } else {
      el.classList.remove('focused');
    }
  });
}

function launchGame(gameId, gameTitle) {
  const overlay = document.getElementById('launchOverlay');
  document.getElementById('launchTitle').innerText = '¡Iniciando ' + (gameTitle || 'juego') + ' en PS5!';
  overlay.style.display = 'flex';

  fetch('/api/play?asin=' + encodeURIComponent(gameId), { method: 'POST' })
    .then(r => r.json())
    .then(res => {
      console.log('Juego lanzado:', res);
      try { window.close(); } catch(e) {}
    })
    .catch(err => {
      console.error('Error lanzando juego:', err);
    });
}

function launchCurrentHero() {
  if (heroGame) launchGame(heroGame.id, heroGame.title);
  else if (catalogGames.length > 0) launchGame(catalogGames[0].id, catalogGames[0].title);
}

function openCurrentHeroInfo() {
  if (heroGame) openModal(heroGame);
  else if (catalogGames.length > 0) openModal(catalogGames[0]);
}

function openModal(game) {
  currentModalGame = game;
  document.getElementById('mTitle').innerText = game.title;
  document.getElementById('mPegi').innerText = game.pegiRating || 'PEGI 12';
  document.getElementById('mPublisher').innerText = game.publisher || 'Amazon Luna';
  document.getElementById('mGenres').innerText = game.genres || 'Cloud Gaming';
  document.getElementById('mSynopsis').innerText = game.synopsis || 'Disfruta de este título en alta fidelidad 1080p 60 FPS mediante streaming de baja latencia WebRTC.';
  
  const bg = game.backdropUrl || game.coverUrl || '';
  document.getElementById('mHero').style.backgroundImage = bg ? 'url(' + bg + ')' : 'none';
  
  document.getElementById('mPlayBtn').onclick = () => {
    closeModal();
    launchGame(game.id, game.title);
  };
  
  const modal = document.getElementById('gameModal');
  modal.style.display = 'flex';
  setTimeout(() => {
    setFocus(1);
  }, 50);
}

function closeModal(e) {
  if (e && e.target && e.target !== document.getElementById('gameModal') && !e.target.classList.contains('btn-close')) {
    return;
  }
  document.getElementById('gameModal').style.display = 'none';
  currentModalGame = null;
  updateFocusables();
}

function renderCatalog(games) {
  catalogGames = games;
  if (!games || games.length === 0) return;

  heroGame = games[0];
  // Mantener el hero banner "Luna Premium" con la imagen oficial /assets/luna_premium_hero.jpg
  document.getElementById('heroBanner').style.backgroundImage = 'url(/assets/luna_premium_hero.jpg)';

  const container = document.getElementById('swimlanesContainer');
  container.innerHTML = '';

  // 1. Fila de Rankings Oficial: Las mejores novedades en juegos
  const rankLane = document.createElement('div');
  rankLane.className = 'swimlane';
  rankLane.innerHTML = '<div class="swimlane-title">Las mejores novedades en juegos</div>';
  const rankRow = document.createElement('div');
  rankRow.className = 'swimlane-row';

  const top10 = games.slice(0, 10);
  top10.forEach((g, idx) => {
    const wrap = document.createElement('div');
    wrap.className = 'game-card-wrapper';
    wrap.innerHTML = '<div class="rank-number">' + (idx + 1) + '</div>';

    const card = document.createElement('div');
    card.className = 'game-card focusable';
    card.tabIndex = 0;
    card.onclick = () => openModal(g);

    card.innerHTML = `
      <img class="card-cover" loading="lazy" src="${g.coverUrl || g.backdropUrl || ''}" alt="${g.title}">
      <div class="card-info">
        <div class="card-title">${g.title}</div>
        <div class="card-sub">${g.publisher || 'Amazon Luna'}</div>
      </div>
    `;
    wrap.appendChild(card);
    rankRow.appendChild(wrap);
  });
  rankLane.appendChild(rankRow);
  container.appendChild(rankLane);

  // 2. Fila Seguir Jugando / Recomendados para ti
  const recLane = document.createElement('div');
  recLane.className = 'swimlane';
  recLane.innerHTML = '<div class="swimlane-title">Recomendados para ti</div>';
  const recRow = document.createElement('div');
  recRow.className = 'swimlane-row';

  games.slice(5).concat(games.slice(0, 5)).forEach(g => {
    const card = document.createElement('div');
    card.className = 'game-card focusable';
    card.tabIndex = 0;
    card.onclick = () => openModal(g);

    card.innerHTML = `
      <img class="card-cover" loading="lazy" src="${g.coverUrl || g.backdropUrl || ''}" alt="${g.title}">
      <div class="card-info">
        <div class="card-title">${g.title}</div>
        <div class="card-sub">${g.genres || g.publisher || 'Cloud Gaming'}</div>
      </div>
    `;
    recRow.appendChild(card);
  });
  recLane.appendChild(recRow);
  container.appendChild(recLane);

  // 3. Fila Acción y Aventura
  if (games.length > 8) {
    const actLane = document.createElement('div');
    actLane.className = 'swimlane';
    actLane.innerHTML = '<div class="swimlane-title">Populares en Luna</div>';
    const actRow = document.createElement('div');
    actRow.className = 'swimlane-row';

    games.slice(2).forEach(g => {
      const card = document.createElement('div');
      card.className = 'game-card focusable';
      card.tabIndex = 0;
      card.onclick = () => openModal(g);

      card.innerHTML = `
        <img class="card-cover" loading="lazy" src="${g.coverUrl || g.backdropUrl || ''}" alt="${g.title}">
        <div class="card-info">
          <div class="card-title">${g.title}</div>
          <div class="card-sub">${g.genres || g.publisher || 'Amazon Luna'}</div>
        </div>
      `;
      actRow.appendChild(card);
    });
    actLane.appendChild(actRow);
    container.appendChild(actLane);
  }

  updateFocusables();
}

function onSearchInput(query) {
  if (!query || query.trim() === '') {
    renderCatalog(catalogGames);
    return;
  }
  const q = query.toLowerCase().trim();
  const filtered = catalogGames.filter(g => 
    (g.title && g.title.toLowerCase().includes(q)) ||
    (g.publisher && g.publisher.toLowerCase().includes(q)) ||
    (g.genres && g.genres.toLowerCase().includes(q))
  );
  renderCatalog(filtered);
}

// Cargar catálogo desde la API local del puente PS5
fetch('/api/catalog')
  .then(r => r.json())
  .then(data => {
    if (data.profile) {
      document.getElementById('profileName').innerText = (data.profile.displayName || 'Bornicho') + ' ' + (data.profile.discriminator || '#8663');
    }
    renderCatalog(data.games || []);
  })
  .catch(err => {
    console.error('Error cargando catálogo:', err);
  });

// DUALSENSE & KEYBOARD NAVIGATION LOOP
let prevButtons = {};
let lastNavTime = 0;

function pollGamepad() {
  const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
  const gp = gamepads[0] || gamepads[1];
  const now = Date.now();

  if (gp && now - lastNavTime > 160) {
    const up = gp.buttons[12]?.pressed || gp.axes[1] < -0.5;
    const down = gp.buttons[13]?.pressed || gp.axes[1] > 0.5;
    const left = gp.buttons[14]?.pressed || gp.axes[0] < -0.5;
    const right = gp.buttons[15]?.pressed || gp.axes[0] > 0.5;

    const btnCross = gp.buttons[0]?.pressed;
    const btnCircle = gp.buttons[1]?.pressed;
    const btnSquare = gp.buttons[2]?.pressed;

    if (right) {
      setFocus(focusIndex + 1);
      lastNavTime = now;
    } else if (left) {
      setFocus(focusIndex - 1);
      lastNavTime = now;
    } else if (down) {
      setFocus(focusIndex + 4);
      lastNavTime = now;
    } else if (up) {
      setFocus(focusIndex - 4);
      lastNavTime = now;
    }

    if (btnCross && !prevButtons[0]) {
      if (focusables[focusIndex]) focusables[focusIndex].click();
      lastNavTime = now + 100;
    }
    if (btnCircle && !prevButtons[1]) {
      closeModal();
      lastNavTime = now + 100;
    }
    if (btnSquare && !prevButtons[2]) {
      document.getElementById('searchInput').focus();
      lastNavTime = now + 100;
    }

    prevButtons[0] = btnCross;
    prevButtons[1] = btnCircle;
    prevButtons[2] = btnSquare;
  }

  requestAnimationFrame(pollGamepad);
}
requestAnimationFrame(pollGamepad);

window.addEventListener('keydown', (e) => {
  if (e.key === 'ArrowRight') { setFocus(focusIndex + 1); e.preventDefault(); }
  else if (e.key === 'ArrowLeft') { setFocus(focusIndex - 1); e.preventDefault(); }
  else if (e.key === 'ArrowDown') { setFocus(focusIndex + 4); e.preventDefault(); }
  else if (e.key === 'ArrowUp') { setFocus(focusIndex - 4); e.preventDefault(); }
  else if (e.key === 'Enter') { if (focusables[focusIndex]) focusables[focusIndex].click(); }
  else if (e.key === 'Escape' || e.key === 'Backspace') { closeModal(); }
});
</script>

</body>
</html>
)raw";
    return html;
}

bool AuthServer::start(int port) {
    stop();
    port_ = port;

#if defined(_WIN32)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#elif defined(LUNA_TARGET_PS5)
    sceNetInit();
    sceNetPoolCreate("luna_auth_pool", 128 * 1024, 0);
#endif

    const int candidatePorts[] = {port, 8080, 8085, 18085, 9085};
    for (int p : candidatePorts) {
        serverSocket_ = openTcpListener(p, serverStatus_);
        if (serverSocket_ >= 0) {
            port_ = p;
            break;
        }
    }

    if (serverSocket_ < 0) {
        luna::util::CrashLogger::log("[Auth Bridge] Fallo al iniciar servidor TCP: %s", serverStatus_.c_str());
        return false;
    }

    isRunning_ = true;
    luna::util::CrashLogger::log("[Auth Bridge] %s", serverStatus_.c_str());
    serverThread_ = std::thread(&AuthServer::serverLoop, this);
    return true;
}

void AuthServer::stop() {
    if (isRunning_.exchange(false)) {
        if (serverSocket_ >= 0) {
#if defined(LUNA_TARGET_PS5)
            sceNetSocketAbort(serverSocket_, 0);
            sceNetSocketClose(serverSocket_);
#else
            CLOSE_SOCKET(serverSocket_);
#endif
            serverSocket_ = -1;
        }
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
#if defined(_WIN32)
        WSACleanup();
#endif
        serverStatus_ = "DETENIDO";
    }
}

void AuthServer::serverLoop() {
    while (isRunning_) {
        sockaddr_in clientAddr{};
#if !defined(_WIN32)
        clientAddr.sin_len = sizeof(sockaddr_in);
#endif
        socklen_t clientLen = sizeof(clientAddr);
#if defined(LUNA_TARGET_PS5)
        int clientSock = sceNetAccept(serverSocket_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (clientSock < 0) {
            clientSock = static_cast<int>(::accept(serverSocket_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen));
        }
#else
        int clientSock = static_cast<int>(::accept(serverSocket_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen));
#endif
        if (clientSock < 0) {
            if (!isRunning_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        handleClient(clientSock);
#if defined(LUNA_TARGET_PS5)
        sceNetSocketClose(clientSock);
#else
        CLOSE_SOCKET(clientSock);
#endif
    }
}

void AuthServer::handleClient(int clientSocket) {
    std::vector<char> buffer(65536, 0);
#if defined(LUNA_TARGET_PS5)
    int bytesRead = sceNetRecv(clientSocket, buffer.data(), buffer.size() - 1, 0);
    if (bytesRead < 0) {
        bytesRead = static_cast<int>(::recv(clientSocket, buffer.data(), buffer.size() - 1, 0));
    }
#else
    int bytesRead = static_cast<int>(::recv(clientSocket, buffer.data(), static_cast<int>(buffer.size()) - 1, 0));
#endif
    if (bytesRead <= 0) return;

    std::string request(buffer.data(), bytesRead);

    auto sendAll = [&](const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            size_t chunk = std::min(data.size() - sent, size_t(16384));
#if defined(LUNA_TARGET_PS5)
            int r = sceNetSend(clientSocket, data.data() + sent, chunk, 0);
            if (r < 0) r = static_cast<int>(::send(clientSocket, data.data() + sent, static_cast<int>(chunk), 0));
#else
            int r = static_cast<int>(::send(clientSocket, data.data() + sent, static_cast<int>(chunk), 0));
#endif
            if (r <= 0) break;
            sent += r;
        }
    };

    auto sendBinary = [&](const uint8_t* data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            size_t chunk = std::min(len - sent, size_t(16384));
#if defined(LUNA_TARGET_PS5)
            int r = sceNetSend(clientSocket, data + sent, chunk, 0);
            if (r < 0) r = static_cast<int>(::send(clientSocket, reinterpret_cast<const char*>(data + sent), static_cast<int>(chunk), 0));
#else
            int r = static_cast<int>(::send(clientSocket, reinterpret_cast<const char*>(data + sent), static_cast<int>(chunk), 0));
#endif
            if (r <= 0) break;
            sent += r;
        }
    };

    // CORS preflight
    if (request.rfind("OPTIONS ", 0) == 0) {
        std::string corsResp = "HTTP/1.1 204 No Content\r\n"
                               "Access-Control-Allow-Origin: *\r\n"
                               "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                               "Access-Control-Allow-Headers: *\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n\r\n";
        sendAll(corsResp);
        return;
    }

    // Official Luna Hero panoramic image
    if (request.rfind("GET /assets/luna_premium_hero.jpg", 0) == 0) {
        std::string header = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: image/jpeg\r\n"
                             "Access-Control-Allow-Origin: *\r\n"
                             "Cache-Control: public, max-age=86400\r\n"
                             "Content-Length: " + std::to_string(luna::assets::kLunaPremiumHeroJpgSize) + "\r\n"
                             "Connection: close\r\n\r\n";
        sendAll(header);
        sendBinary(luna::assets::kLunaPremiumHeroJpg, luna::assets::kLunaPremiumHeroJpgSize);
        return;
    }

    // Official Tempo /token auth endpoint
    if (request.rfind("GET /token", 0) == 0) {
        std::string token;
        {
            std::lock_guard<std::mutex> lk(dataMutex_);
            token = accessToken_;
        }
        std::string json = "{\"valid\":true,\"token\":\"" + escapeJsonString(token) + "\"}";
        std::string resp = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json; charset=utf-8\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Content-Length: " + std::to_string(json.size()) + "\r\n"
                           "Connection: close\r\n\r\n" + json;
        sendAll(resp);
        return;
    }

    // Official Tempo /cookie endpoint
    if (request.rfind("GET /cookie", 0) == 0) {
        std::string json = "{\"interpretedStringState\":\"CONSENT_GIVEN\"}";
        std::string resp = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json; charset=utf-8\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Content-Length: " + std::to_string(json.size()) + "\r\n"
                           "Connection: close\r\n\r\n" + json;
        sendAll(resp);
        return;
    }

    // Web manifest
    if (request.rfind("GET /manifest-branded.json", 0) == 0) {
        std::string json = "{\"name\":\"Amazon Luna\",\"short_name\":\"Luna\",\"start_url\":\"/luna\",\"display\":\"standalone\",\"background_color\":\"#0a0714\",\"theme_color\":\"#9146FF\"}";
        std::string resp = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json; charset=utf-8\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Content-Length: " + std::to_string(json.size()) + "\r\n"
                           "Connection: close\r\n\r\n" + json;
        sendAll(resp);
        return;
    }

    // Proxy endpoint for Tempo backend (LG Smart TV mode)
    if (request.rfind("POST /tempo/", 0) == 0 || request.rfind("GET /tempo/", 0) == 0) {
        bool isPost = (request.rfind("POST /tempo/", 0) == 0);
        size_t pathStart = request.find(' ');
        if (pathStart != std::string::npos) pathStart++;
        size_t pathEnd = request.find(' ', pathStart);
        std::string reqPath = (pathEnd != std::string::npos) ? request.substr(pathStart, pathEnd - pathStart) : "/tempo/";
        
        std::string apiPath = reqPath;
        if (apiPath.rfind("/tempo", 0) == 0) {
            apiPath = apiPath.substr(6);
        }
        if (apiPath.empty() || apiPath[0] != '/') apiPath = "/" + apiPath;

        std::string token;
        {
            std::lock_guard<std::mutex> lk(dataMutex_);
            token = accessToken_;
        }

        std::vector<std::pair<std::string, std::string>> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json"},
            {"x-amz-platform", "smart_tv_web"},
            {"x-amz-device-type", "A2U9WJDSJD7QG9"},
            {"x-amz-luna-app-version", "1.0"}
        };
        if (!token.empty()) {
            headers.push_back({"x-amz-access-token", token});
        }

        std::string targetUrl = "https://proxy-prod.eu-west-1.tempo.digital.a2z.com" + apiPath;
        luna::net::HttpResponse proxyResp;
        if (isPost) {
            std::string postBody;
            size_t bodyPos = request.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                postBody = request.substr(bodyPos + 4);
            }
            proxyResp = luna::net::HttpClient::post(targetUrl, postBody, headers);
        } else {
            proxyResp = luna::net::HttpClient::get(targetUrl, headers);
        }

        std::string statusLine = "HTTP/1.1 " + std::to_string(proxyResp.statusCode > 0 ? proxyResp.statusCode : 200) + " OK\r\n";
        std::string resp = statusLine +
                           "Content-Type: application/json; charset=utf-8\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Content-Length: " + std::to_string(proxyResp.body.size()) + "\r\n"
                           "Connection: close\r\n\r\n" + proxyResp.body;
        sendAll(resp);
        return;
    }

    if (request.rfind("GET /luna", 0) == 0) {
        std::string body = generateLunaWebPage();
        std::string respStr = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Content-Length: " + std::to_string(body.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + body;
        sendAll(respStr);
    } else if (request.rfind("GET /api/catalog", 0) == 0) {
        std::string json;
        {
            std::lock_guard<std::mutex> lk(dataMutex_);
            json = generateCatalogJson();
        }
        std::string respStr = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json; charset=utf-8\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Content-Length: " + std::to_string(json.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + json;
        sendAll(respStr);
    } else if (request.rfind("GET /api/page", 0) == 0) {
        std::string pageType = extractQueryParam(request, "type");
        std::string pageId = extractQueryParam(request, "id");
        if (pageType.empty()) pageType = "landing_home";
        if (pageId.empty()) pageId = "landing_home";

        std::string rawJson = "{}";
        {
            std::lock_guard<std::mutex> lk(dataMutex_);
            if (sessionClient_) {
                rawJson = sessionClient_->fetchRawPage(pageType, pageId, accessToken_);
            }
        }
        std::string respStr = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json; charset=utf-8\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Content-Length: " + std::to_string(rawJson.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + rawJson;
        sendAll(respStr);
    } else if (request.find("/api/play") != std::string::npos) {
        std::string asin = extractQueryParam(request, "asin");
        if (asin.empty()) {
            size_t bodyPos = request.find("\r\n\r\n");
            if (bodyPos != std::string::npos) {
                std::string body = request.substr(bodyPos + 4);
                asin = extractJsonString(body, "asin");
                if (asin.empty()) asin = extractFormField(body, "asin");
            }
        }

        luna::util::CrashLogger::log("[Auth Bridge] Recibida peticion /api/play para asin=%s", asin.c_str());
        if (!asin.empty() && playCb_) {
            playCb_(asin);
        }

        std::string jsonResp = "{\"ok\":true,\"asin\":\"" + escapeJsonString(asin) + "\",\"message\":\"Iniciando juego nativo en PS5\"}";
        std::string respStr = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json; charset=utf-8\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Content-Length: " + std::to_string(jsonResp.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + jsonResp;
        sendAll(respStr);
    } else if (request.rfind("POST /api/session", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::string rawJson = extractFormField(body, "sessionJson");
            if (rawJson.empty()) {
                rawJson = body;
            }

            if (parseLunaSession(rawJson, currentSession_)) {
                serverStatus_ = "SESION LUNA OK: " + currentSession_.publicIp + ":" + std::to_string(currentSession_.port);
                luna::util::CrashLogger::log("[Auth Bridge] %s", serverStatus_.c_str());
                if (sessionCb_) {
                    sessionCb_(currentSession_);
                }
            } else {
                serverStatus_ = "ERROR PARSEANDO SESION LUNA";
                luna::util::CrashLogger::log("[Auth Bridge] %s", serverStatus_.c_str());
            }
        }

        std::string okBody = "<!DOCTYPE html><html><body style='background:#0a0714;color:#34d399;font-family:sans-serif;text-align:center;padding:40px'><h2>Sesion Luna procesada con exito!</h2><p>Conectando WebRTC a " + currentSession_.publicIp + ":" + std::to_string(currentSession_.port) + "</p><p>Mira la pantalla de tu TV.</p></body></html>";
        std::string respStr = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: " + std::to_string(okBody.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + okBody;
        sendAll(respStr);
    } else if (request.rfind("GET /", 0) == 0) {
        std::string body = generateHtmlPage();
        std::string respStr = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: " + std::to_string(body.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + body;
        sendAll(respStr);
    } else {
        std::string notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        sendAll(notFound);
    }
}

} // namespace luna::auth
'''

with open('apps/luna-ps5/src/auth/auth_server.cpp', 'w', encoding='utf-8') as f:
    f.write(new_auth_server_content)

print('Updated apps/luna-ps5/src/auth/auth_server.cpp, size:', len(new_auth_server_content))
