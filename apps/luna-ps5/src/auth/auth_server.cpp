#include "auth/auth_server.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>

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

std::string extractFormField(const std::string& body, const std::string& key) {
    std::string pattern = key + "=";
    size_t start = body.find(pattern);
    if (start == std::string::npos) return {};
    start += pattern.size();
    size_t end = body.find('&', start);
    std::string raw = (end == std::string::npos) ? body.substr(start) : body.substr(start, end - start);
    return urlDecode(raw);
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

    // signalingResponse puede venir anidada o con su SDP escapado
    std::string sig = extractJsonString(raw, "signalingResponse");
    if (!sig.empty()) {
        s.sdpAnswer = extractJsonString(sig, "sdp");
        if (s.sdpAnswer.empty()) {
            s.sdpAnswer = sig; // Si viene el SDP directamente
        }
    }
    if (s.sdpAnswer.empty()) {
        s.sdpAnswer = extractJsonString(raw, "sdp");
    }

    // Extraer puerto del SDP o candidate
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

    // Extraer ice-ufrag y ice-pwd
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
    int lrc = sceNetListen(s, 8);
    if (lrc < 0) {
        lrc = ::listen(s, 8);
    }
#else
    int lrc = ::listen(s, 8);
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

    statusOut = "ACTIVO EN PUERTO TCP " + std::to_string(port) + " (ESCUCHANDO LAN)";
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
    html += "<title>Amazon Luna PS5 - Vinculacion WebRTC</title>";
    html += "<style>";
    html += "body{font-family:system-ui,-apple-system,sans-serif;background:#0e0720;color:#f8fafc;padding:20px;margin:0}";
    html += ".card{background:#180d35;border:2px solid #7c3aed;border-radius:12px;max-width:580px;margin:16px auto;padding:24px;box-shadow:0 10px 30px rgba(0,0,0,0.5)}";
    html += "h2{color:#c4b5fd;margin-top:0;font-size:24px}.btn{background:#10b981;color:#052e16;border:none;padding:14px 20px;border-radius:8px;cursor:pointer;font-weight:800;width:100%;font-size:16px;transition:0.2s}";
    html += ".btn:hover{background:#34d399}textarea{width:100%;box-sizing:border-box;background:#090414;border:1px solid #4c1d95;color:#a7f3d0;padding:12px;border-radius:8px;margin:8px 0 18px;font-size:13px;font-family:monospace}";
    html += ".status{padding:12px 14px;background:#1e1b4b;border-left:4px solid #10b981;border-radius:6px;margin-bottom:20px;color:#a7f3d0}";
    html += ".info{background:#2e1065;border:1px solid #6b21a8;border-radius:8px;padding:12px;margin-bottom:16px;font-size:13px;line-height:1.5;color:#e9d5ff}";
    html += "label{font-weight:600;color:#ddd6fe;display:block}";
    html += "</style></head><body>";
    html += "<div class='card'>";
    html += "<h2>Amazon Luna PS5 (PPSA99825)</h2>";
    html += "<div class='status'><strong>Conectado a tu PS5:</strong> Lista para recibir stream WebRTC</div>";
    html += "<div class='info'><strong>Instrucciones rapidas:</strong><br>";
    html += "1. En tu PC/movil con Luna, pulsa F12 &rarr; Red.<br>";
    html += "2. Inicia un juego y copia la respuesta de <code>getGameSessionJob</code>.<br>";
    html += "3. Pegala aqui debajo y dale a Iniciar.</div>";
    html += "<form method='POST' action='/api/session'>";
    html += "<label>Respuesta JSON de getGameSessionJob:</label>";
    html += "<textarea name='sessionJson' rows='8' placeholder='{\"gameSession\":{\"publicIp\":\"15.217.4.14\",\"signalingResponse\":...}}' required></textarea>";
    html += "<button type='submit' class='btn'>INICIAR STREAM EN LA PS5</button>";
    html += "</form>";
    html += "</div></body></html>";
    return html;
}

bool AuthServer::start(int port) {
    port_ = port;

#if defined(_WIN32)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#elif defined(LUNA_TARGET_PS5)
    sceNetInit();
    sceNetPoolCreate("luna_auth_pool", 64 * 1024, 0);
#endif

    // Intentar el puerto solicitado y, si el sistema lo reserva, probar alternativos
    const int candidatePorts[] = {port, 18085, 8080, 9085};
    for (int p : candidatePorts) {
        serverSocket_ = openTcpListener(p, serverStatus_);
        if (serverSocket_ >= 0) {
            port_ = p;
            break;
        }
    }

    if (serverSocket_ < 0) {
        std::printf("[Auth Bridge] %s\n", serverStatus_.c_str());
        return false;
    }

    isRunning_ = true;
    std::printf("[Auth Bridge] %s\n", serverStatus_.c_str());
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
    std::vector<char> buffer(32768, 0);
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

    if (request.rfind("GET /", 0) == 0) {
        std::string body = generateHtmlPage();
        std::string respStr = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: " + std::to_string(body.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + body;
        sendAll(respStr);
    } else if (request.rfind("POST /api/session", 0) == 0) {
        size_t bodyPos = request.find("\r\n\r\n");
        if (bodyPos != std::string::npos) {
            std::string body = request.substr(bodyPos + 4);
            std::string rawJson = extractFormField(body, "sessionJson");
            if (rawJson.empty()) {
                rawJson = body; // JSON enviado directamente por curl o script
            }

            if (parseLunaSession(rawJson, currentSession_)) {
                serverStatus_ = "SESION LUNA OK: " + currentSession_.publicIp + ":" + std::to_string(currentSession_.port);
                std::printf("[Auth Bridge] %s\n", serverStatus_.c_str());
                if (sessionCb_) {
                    sessionCb_(currentSession_);
                }
            } else {
                serverStatus_ = "ERROR PARSEANDO SESION LUNA";
                std::printf("[Auth Bridge] %s\n", serverStatus_.c_str());
            }
        }

        std::string okBody = "<!DOCTYPE html><html><body style='background:#0e0720;color:#34d399;font-family:sans-serif;text-align:center;padding:40px'><h2>Sesion Luna procesada con exito!</h2><p>Conectando WebRTC a " + currentSession_.publicIp + ":" + std::to_string(currentSession_.port) + "</p><p>Mira la pantalla de tu TV.</p></body></html>";
        std::string respStr = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html; charset=utf-8\r\n"
                              "Content-Length: " + std::to_string(okBody.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + okBody;
        sendAll(respStr);
    } else {
        std::string notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        sendAll(notFound);
    }
}

} // namespace luna::auth
