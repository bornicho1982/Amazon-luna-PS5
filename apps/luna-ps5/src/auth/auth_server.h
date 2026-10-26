#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace luna::session { class LunaSessionClient; }
namespace luna::display { struct GameTile; }

namespace luna::auth {

struct AuthSession {
    std::string sessionId;
    std::string gameId;
    std::string publicIp;
    int port = 0;
    std::string sdpAnswer;
    std::string iceUfrag;
    std::string icePwd;
    std::string rawJson;
    bool isValid = false;
};

using SessionCallback = std::function<void(const AuthSession& session)>;
using PlayCallback = std::function<void(const std::string& asin)>;

class AuthServer {
public:
    AuthServer();
    ~AuthServer();

    bool start(int port = 8080);
    void stop();

    void onSessionReceived(SessionCallback cb) { sessionCb_ = std::move(cb); }
    void onPlayRequested(PlayCallback cb) { playCb_ = std::move(cb); }

    void setSessionClient(session::LunaSessionClient* client, const std::string& accessToken) {
        std::lock_guard<std::mutex> lk(dataMutex_);
        sessionClient_ = client;
        accessToken_ = accessToken;
    }

    void setCatalog(const std::vector<display::GameTile>* catalog) {
        std::lock_guard<std::mutex> lk(dataMutex_);
        catalog_ = catalog;
    }

    const AuthSession& getSession() const { return currentSession_; }
    bool hasValidSession() const { return currentSession_.isValid; }
    bool isListening() const { return isRunning_.load(); }
    int getPort() const { return port_; }
    const std::string& getServerStatus() const { return serverStatus_; }

private:
    void serverLoop();
    void handleClient(int clientSocket);
    std::string generateHtmlPage() const;
    std::string generateLunaWebPage() const;
    std::string generateCatalogJson() const;

    int port_ = 8080;
    std::atomic<bool> isRunning_{false};
    int serverSocket_ = -1;
    std::thread serverThread_;
    std::string serverStatus_ = "INICIANDO...";

    AuthSession currentSession_;
    SessionCallback sessionCb_;
    PlayCallback playCb_;

    std::mutex dataMutex_;
    session::LunaSessionClient* sessionClient_ = nullptr;
    std::string accessToken_;
    const std::vector<display::GameTile>* catalog_ = nullptr;
};

} // namespace luna::auth
