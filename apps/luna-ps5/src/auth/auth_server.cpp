#include "auth/auth_server.h"
#include "session/luna_session.h"
#include "display/display_manager.h"
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
    html += "body{font-family:system-ui,-apple-system,sans-serif;background:#0e0720;color:#f8fafc;padding:20px;margin:0}";
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
<title>Amazon Luna — PlayStation 5 Web Bridge</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background-color: #0E0B16;
    color: #FFFFFF;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    overflow-x: hidden;
  }
  header {
    position: sticky;
    top: 0;
    z-index: 100;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 40px;
    background: rgba(14, 11, 22, 0.95);
    border-bottom: 1px solid #231B38;
    backdrop-filter: blur(8px);
  }
  .logo-area { display: flex; align-items: center; gap: 12px; }
  .logo-badge {
    width: 24px; height: 24px; background: linear-gradient(135deg, #02FDDB, #9146FF);
    border-radius: 6px; transform: rotate(45deg);
  }
  .logo-text { font-size: 24px; font-weight: 800; letter-spacing: -0.5px; }
  nav { display: flex; gap: 24px; font-size: 15px; font-weight: 600; color: #94A3B8; }
  nav a { color: inherit; text-decoration: none; cursor: pointer; transition: 0.2s; }
  nav a.active, nav a:hover { color: #FFFFFF; }
  .status-pill {
    background: rgba(16, 185, 129, 0.15);
    color: #10B981;
    border: 1px solid #10B981;
    padding: 6px 14px;
    border-radius: 20px;
    font-size: 13px;
    font-weight: 700;
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .status-pill::before {
    content: '';
    width: 8px; height: 8px;
    background: #10B981;
    border-radius: 50%;
    box-shadow: 0 0 8px #10B981;
  }
  .hero-banner {
    position: relative;
    width: 100%;
    height: 480px;
    background-size: cover;
    background-position: center;
    display: flex;
    align-items: flex-end;
    padding: 40px 60px;
  }
  .hero-gradient {
    position: absolute;
    inset: 0;
    background: linear-gradient(90deg, #0E0B16 0%, rgba(14,11,22,0.85) 45%, rgba(14,11,22,0.1) 100%),
                linear-gradient(0deg, #0E0B16 0%, transparent 60%);
  }
  .hero-content {
    position: relative;
    z-index: 10;
    max-width: 650px;
  }
  .hero-badge {
    display: inline-block;
    background: #9146FF;
    color: #FFF;
    padding: 4px 10px;
    border-radius: 4px;
    font-size: 12px;
    font-weight: 700;
    margin-bottom: 12px;
  }
  .hero-title { font-size: 40px; font-weight: 800; line-height: 1.15; margin-bottom: 12px; }
  .hero-desc { font-size: 15px; color: #CBD5E1; line-height: 1.5; margin-bottom: 24px; }
  .btn-play {
    background: #FFFFFF;
    color: #0E0B16;
    border: none;
    padding: 14px 28px;
    border-radius: 30px;
    font-size: 16px;
    font-weight: 800;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 10px;
    box-shadow: 0 4px 14px rgba(255,255,255,0.3);
    transition: transform 0.15s, background 0.15s;
  }
  .btn-play:hover { background: #E2E8F0; transform: scale(1.03); }
  .btn-play::before {
    content: '▶';
    font-size: 14px;
    color: #9146FF;
  }
  .swimlane { padding: 24px 40px; }
  .swimlane-title { font-size: 22px; font-weight: 800; margin-bottom: 16px; color: #FFFFFF; }
  .cards-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
    gap: 20px;
  }
  .game-card {
    background: #1C152E;
    border-radius: 12px;
    overflow: hidden;
    cursor: pointer;
    transition: transform 0.2s, box-shadow 0.2s;
    border: 2px solid transparent;
  }
  .game-card:hover {
    transform: translateY(-6px);
    box-shadow: 0 10px 24px rgba(145, 70, 255, 0.4);
    border-color: #9146FF;
  }
  .card-cover {
    width: 100%;
    aspect-ratio: 3/4;
    object-fit: cover;
    background: #231B38;
    display: block;
  }
  .card-info { padding: 12px; }
  .card-title { font-size: 14px; font-weight: 700; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .card-sub { font-size: 12px; color: #94A3B8; margin-top: 4px; }
  .modal-backdrop {
    display: none;
    position: fixed;
    inset: 0;
    z-index: 200;
    background: rgba(14, 11, 22, 0.85);
    backdrop-filter: blur(10px);
    align-items: center;
    justify-content: center;
  }
  .modal {
    background: #1C152E;
    border: 1px solid #9146FF;
    border-radius: 16px;
    max-width: 700px;
    width: 90%;
    overflow: hidden;
    box-shadow: 0 20px 50px rgba(0,0,0,0.8);
  }
  .modal-header-img { width: 100%; height: 260px; object-fit: cover; background: #231B38; }
  .modal-body { padding: 24px; }
  .modal-title { font-size: 28px; font-weight: 800; margin-bottom: 8px; }
  .modal-meta { font-size: 14px; color: #94A3B8; margin-bottom: 16px; }
  .modal-synopsis { font-size: 14px; color: #CBD5E1; line-height: 1.6; margin-bottom: 24px; max-height: 140px; overflow-y: auto; }
  .modal-actions { display: flex; justify-content: space-between; align-items: center; }
  .btn-close { background: transparent; border: 1px solid #4B3A70; color: #CBD5E1; padding: 12px 24px; border-radius: 30px; font-weight: 700; cursor: pointer; }
  .toast {
    display: none;
    position: fixed;
    bottom: 30px;
    left: 50%;
    transform: translateX(-50%);
    background: #10B981;
    color: #052E16;
    padding: 16px 32px;
    border-radius: 40px;
    font-weight: 800;
    font-size: 16px;
    box-shadow: 0 10px 30px rgba(16, 185, 129, 0.5);
    z-index: 300;
  }
</style>
</head>
<body>
<header>
  <div class="logo-area">
    <div class="logo-badge"></div>
    <div class="logo-text">luna</div>
  </div>
  <nav>
    <a class="active">Inicio</a>
    <a>Biblioteca</a>
    <a>Prime Gaming</a>
    <a>Luna+</a>
  </nav>
  <div class="status-pill">PS5 Lista (1080p60)</div>
</header>

<div class="hero-banner" id="heroBanner">
  <div class="hero-gradient"></div>
  <div class="hero-content">
    <div class="hero-badge" id="heroBadge">DESTACADO EN AMAZON LUNA</div>
    <div class="hero-title" id="heroTitle">Cargando catálogo oficial...</div>
    <div class="hero-desc" id="heroDesc">Conectando con el catálogo de tu cuenta en la consola PlayStation 5...</div>
    <button class="btn-play" id="heroPlayBtn" onclick="launchCurrentHero()">Jugar ahora en PS5</button>
  </div>
</div>

<div class="swimlane">
  <div class="swimlane-title">Todos los juegos disponibles</div>
  <div class="cards-grid" id="gamesGrid"></div>
</div>

<div class="modal-backdrop" id="gameModal" onclick="closeModal(event)">
  <div class="modal" onclick="event.stopPropagation()">
    <img id="mCover" class="modal-header-img" src="" alt="Game Art">
    <div class="modal-body">
      <div class="modal-title" id="mTitle"></div>
      <div class="modal-meta" id="mMeta"></div>
      <div class="modal-synopsis" id="mSynopsis"></div>
      <div class="modal-actions">
        <button class="btn-close" onclick="closeModal()">Cerrar</button>
        <button class="btn-play" id="mPlayBtn">Jugar ahora en PS5</button>
      </div>
    </div>
  </div>
</div>

<div class="toast" id="toast"></div>

<script>
let currentGames = [];
let currentHeroGame = null;

function showToast(msg) {
  const t = document.getElementById('toast');
  t.innerText = msg;
  t.style.display = 'block';
  setTimeout(() => { t.style.display = 'none'; }, 4500);
}

function launchGame(gameId, gameTitle) {
  showToast('🚀 Lanzando ' + (gameTitle || 'juego') + ' en tu PS5 a 1080p60...');
  fetch('/api/play?asin=' + encodeURIComponent(gameId), { method: 'POST' })
    .then(r => r.json())
    .then(data => {
      showToast('🎮 ¡Partida iniciada en PS5! Conectando WebRTC nativo...');
      if (document.getElementById('gameModal')) closeModal();
    })
    .catch(err => {
      console.error(err);
      showToast('Error de comunicación con el puente PS5');
    });
}

function launchCurrentHero() {
  if (currentHeroGame) {
    launchGame(currentHeroGame.id, currentHeroGame.title);
  }
}

function openModal(game) {
  document.getElementById('mTitle').innerText = game.title;
  document.getElementById('mMeta').innerText = (game.publisher || '') + ' • ' + (game.genres || 'Cloud Gaming') + ' • ' + (game.pegiRating || 'PEGI');
  document.getElementById('mSynopsis').innerText = game.synopsis || 'Disfruta de este título en alta fidelidad 1080p 60 FPS mediante decodificación por hardware libSceVideodec2.';
  document.getElementById('mCover').src = game.backdropUrl || game.coverUrl || '';
  document.getElementById('mPlayBtn').onclick = () => launchGame(game.id, game.title);
  document.getElementById('gameModal').style.display = 'flex';
}

function closeModal() {
  document.getElementById('gameModal').style.display = 'none';
}

fetch('/api/catalog')
  .then(r => r.json())
  .then(data => {
    currentGames = data.games || [];
    if (currentGames.length > 0) {
      currentHeroGame = currentGames[0];
      document.getElementById('heroTitle').innerText = currentHeroGame.title;
      document.getElementById('heroDesc').innerText = currentHeroGame.synopsis || (currentHeroGame.publisher + ' • ' + currentHeroGame.category);
      if (currentHeroGame.backdropUrl || currentHeroGame.coverUrl) {
        document.getElementById('heroBanner').style.backgroundImage = 'url(' + (currentHeroGame.backdropUrl || currentHeroGame.coverUrl) + ')';
      }

      const grid = document.getElementById('gamesGrid');
      grid.innerHTML = '';
      currentGames.forEach(g => {
        const card = document.createElement('div');
        card.className = 'game-card';
        card.onclick = () => openModal(g);

        const img = document.createElement('img');
        img.className = 'card-cover';
        img.loading = 'lazy';
        img.src = g.coverUrl || g.backdropUrl || '';

        const info = document.createElement('div');
        info.className = 'card-info';

        const t = document.createElement('div');
        t.className = 'card-title';
        t.innerText = g.title;

        const p = document.createElement('div');
        p.className = 'card-sub';
        p.innerText = g.publisher || g.category || 'Amazon Luna';

        info.appendChild(t);
        info.appendChild(p);
        card.appendChild(img);
        card.appendChild(info);
        grid.appendChild(card);
      });
    }
  })
  .catch(err => {
    console.error('Error cargando catalogo:', err);
    document.getElementById('heroTitle').innerText = 'STAR WARS: Galactic Racer';
  });
</script>
</body>
</html>
)raw";
    return html;
}

bool AuthServer::start(int port) {
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
#if defined(LUNA_TARGET_PS5)
        int r = sceNetSend(clientSocket, data.data(), data.size(), 0);
        if (r < 0) ::send(clientSocket, data.data(), static_cast<int>(data.size()), 0);
#else
        ::send(clientSocket, data.data(), static_cast<int>(data.size()), 0);
#endif
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

        std::string okBody = "<!DOCTYPE html><html><body style='background:#0e0720;color:#34d399;font-family:sans-serif;text-align:center;padding:40px'><h2>Sesion Luna procesada con exito!</h2><p>Conectando WebRTC a " + currentSession_.publicIp + ":" + std::to_string(currentSession_.port) + "</p><p>Mira la pantalla de tu TV.</p></body></html>";
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
