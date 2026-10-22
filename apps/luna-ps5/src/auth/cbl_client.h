#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace luna::auth {

enum class CblState {
    Uninitialized,
    CheckingStorage,
    RequestingCode,
    WaitingForUser,
    Authenticated,
    Error
};

struct AccountTokens {
    std::string accessToken;
    std::string refreshToken;
    std::string customerId;
    int64_t expiresAt = 0; // Unix timestamp
};

class CblClient {
public:
    CblClient();
    ~CblClient();

    bool init(const std::string& storageDir);
    void tick(); // Polling update called each frame or timer
    void logout();
    bool refreshAccessToken();

    CblState getState() const { return m_state; }
    bool isAuthenticated() const { return m_state == CblState::Authenticated; }

    std::string getPublicCode() const { return m_publicCode; }
    std::string getVerificationUri() const { return "amazon.com/code"; }
    std::string getAccessToken() const { return m_tokens.accessToken; }
    std::string getCustomerId() const { return m_tokens.customerId; }
    std::string getDeviceSerial() const { return m_deviceSerial; }
    std::string getErrorMessage() const { return m_errorMessage; }

    // QR Code data (size x size matrix of bools)
    int getQrSize() const { return m_qrSize; }
    bool getQrModule(int x, int y) const;

private:
    bool loadAccountFile();
    bool saveAccountFile();
    bool requestCblCodePair();
    bool pollRegistration();
    void generateQrCode();
    std::string getOrGenerateDeviceSerial();

    std::string m_storageDir;
    std::string m_storagePath;
    std::string m_deviceSerial;
    CblState m_state = CblState::Uninitialized;
    AccountTokens m_tokens;

    std::string m_publicCode;
    std::string m_privateCode;
    int m_pollingIntervalSec = 3;
    int m_expiresInSec = 600;
    std::chrono::steady_clock::time_point m_codeAcquiredTime;
    std::chrono::steady_clock::time_point m_lastPollTime;

    std::string m_errorMessage;

    // QR Code buffer
    int m_qrSize = 0;
    std::vector<uint8_t> m_qrCode;
};

} // namespace luna::auth
