#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>

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

class AuthServer {
public:
    AuthServer();
    ~AuthServer();

    bool start(int port = 8085);
    void stop();

    void onSessionReceived(SessionCallback cb) { sessionCb_ = std::move(cb); }
    const AuthSession& getSession() const { return currentSession_; }
    bool hasValidSession() const { return currentSession_.isValid; }
    bool isListening() const { return isRunning_.load(); }
    int getPort() const { return port_; }
    const std::string& getServerStatus() const { return serverStatus_; }

private:
    void serverLoop();
    void handleClient(int clientSocket);
    std::string generateHtmlPage() const;

    int port_ = 8085;
    std::atomic<bool> isRunning_{false};
    int serverSocket_ = -1;
    std::thread serverThread_;
    std::string serverStatus_ = "INICIANDO...";

    AuthSession currentSession_;
    SessionCallback sessionCb_;
};

} // namespace luna::auth
