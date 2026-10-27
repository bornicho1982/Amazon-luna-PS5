#include "net/http_client.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <atomic>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include "net/sce_http.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
extern "C" uint64_t scePthreadSelf(void);
#endif

namespace luna::net {

namespace {

class AtomicRecursiveLock {
    std::atomic<uint64_t> owner_{0};
    uint32_t count_{0};
public:
    void lock() {
#if defined(_WIN32)
        uint64_t tid = static_cast<uint64_t>(GetCurrentThreadId());
#else
        uint64_t tid = scePthreadSelf();
#endif
        if (owner_.load(std::memory_order_relaxed) == tid) {
            count_++;
            return;
        }
        uint64_t expected = 0;
        while (!owner_.compare_exchange_weak(expected, tid, std::memory_order_acquire, std::memory_order_relaxed)) {
            expected = 0;
#if defined(_WIN32)
            Sleep(1);
#else
            struct timespec ts = {0, 1000000}; // 1ms
            nanosleep(&ts, nullptr);
#endif
        }
        count_ = 1;
    }

    void unlock() {
        if (--count_ == 0) {
            owner_.store(0, std::memory_order_release);
        }
    }
};

class AtomicRecursiveGuard {
    AtomicRecursiveLock& lock_;
public:
    explicit AtomicRecursiveGuard(AtomicRecursiveLock& l) : lock_(l) { lock_.lock(); }
    ~AtomicRecursiveGuard() { lock_.unlock(); }
};

static AtomicRecursiveLock g_httpLock;

#if !defined(_WIN32)
int g_netPoolId = -1;
int g_sslCtxId = -1;
int g_httpCtxId = -1;
int g_tmplId = -1;
bool g_initialized = false;
#endif

#if defined(_WIN32)
std::wstring toWString(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], size);
    return out;
}
#endif

static std::string trimString(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

} // namespace

bool HttpClient::init() {
    AtomicRecursiveGuard lock(g_httpLock);
#if defined(_WIN32)
    return true;
#else
    if (g_initialized) return true;

    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);

    sceNetInit();

    g_netPoolId = sceNetPoolCreate("luna_net", 1024 * 1024, 0);
    if (g_netPoolId < 0) {
        char dynamicName[32];
        std::snprintf(dynamicName, sizeof(dynamicName), "luna_%u", static_cast<unsigned>(time(nullptr) & 0xFFFF));
        g_netPoolId = sceNetPoolCreate(dynamicName, 1024 * 1024, 0);
    }

    g_sslCtxId = sceSslInit(1024 * 1024);
    if (g_sslCtxId < 0) {
        g_sslCtxId = sceSslInit(512 * 1024);
    }

    g_httpCtxId = sceHttpInit(g_netPoolId, g_sslCtxId, 1024 * 1024);

    if (g_httpCtxId >= 0) {
        g_tmplId = sceHttpCreateTemplate(g_httpCtxId, "Mozilla/5.0 (PlayStation 5 1.0) AppleWebKit/605.1.15 (KHTML, like Gecko)", SCE_HTTP_VERSION_1_1, 1);
        if (g_tmplId >= 0) {
            sceHttpsDisableOption(g_tmplId, 0x01);
            sceHttpsDisableOption(g_tmplId, 0x02);
            sceHttpSetAutoRedirect(g_tmplId, 1);
            sceHttpSetResolveTimeOut(g_tmplId, 10000000); // 10s
            sceHttpSetResolveRetry(g_tmplId, 3);
            sceHttpSetConnectTimeOut(g_tmplId, 30000000); // 30s
            sceHttpSetSendTimeOut(g_tmplId, 30000000);    // 30s
            sceHttpSetRecvTimeOut(g_tmplId, 60000000);    // 60s (evita HTTP 499 por cierre prematuro)
            g_initialized = true;
        }
    }
    return g_initialized;
#endif
}

void HttpClient::shutdown() {
    AtomicRecursiveGuard lock(g_httpLock);
#if defined(_WIN32)
    // No-op
#else
    if (!g_initialized) return;
    if (g_tmplId >= 0) {
        sceHttpDeleteTemplate(g_tmplId);
        g_tmplId = -1;
    }
    if (g_httpCtxId >= 0) {
        sceHttpTerm(g_httpCtxId);
        g_httpCtxId = -1;
    }
    if (g_sslCtxId >= 0) {
        sceSslTerm(g_sslCtxId);
        g_sslCtxId = -1;
    }
    if (g_netPoolId >= 0) {
        sceNetPoolDestroy(g_netPoolId);
        g_netPoolId = -1;
    }
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    g_initialized = false;
#endif
}

HttpResponse HttpClient::post(const std::string& url,
                             const std::string& jsonBody,
                             const std::vector<std::pair<std::string, std::string>>& headers) {
    AtomicRecursiveGuard lock(g_httpLock);
    HttpResponse resp;

#if defined(_WIN32)
    HINTERNET hSession = WinHttpOpen(L"Luna-Client/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        resp.error = "WinHttpOpen failed";
        return resp;
    }

    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256]{};
    wchar_t urlPath[1024]{};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName) / sizeof(wchar_t);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(wchar_t);

    std::wstring wUrl = toWString(url);
    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        resp.error = "Invalid URL";
        return resp;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        resp.error = "WinHttpConnect failed";
        return resp;
    }

    DWORD reqFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath,
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.error = "WinHttpOpenRequest failed";
        return resp;
    }

    std::wstring allHeaders;
    for (const auto& h : headers) {
        allHeaders += toWString(h.first) + L": " + toWString(h.second) + L"\r\n";
    }

    BOOL sendOk = WinHttpSendRequest(hRequest,
                                     allHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : allHeaders.c_str(),
                                     (DWORD)allHeaders.length(),
                                     (LPVOID)jsonBody.data(),
                                     (DWORD)jsonBody.size(),
                                     (DWORD)jsonBody.size(), 0);

    if (sendOk && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        resp.statusCode = (int)statusCode;

        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        std::vector<char> buffer;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            buffer.resize(dwSize);
            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                resp.body.append(buffer.data(), dwDownloaded);
            }
        } while (dwSize > 0);
    } else {
        resp.error = "WinHttpSendRequest or ReceiveResponse failed";
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;

#else
    if (!init()) {
        char errBuf[128];
        std::snprintf(errBuf, sizeof(errBuf), "HttpClient init error (pool=%d, ssl=%d, http=%d, tmpl=%d)",
                      g_netPoolId, g_sslCtxId, g_httpCtxId, g_tmplId);
        resp.error = errBuf;
        return resp;
    }

    int connId = sceHttpCreateConnectionWithURL(g_tmplId, url.c_str(), 0);
    if (connId < 0) {
        resp.error = "sceHttpCreateConnectionWithURL failed: " + std::to_string(connId);
        return resp;
    }

    sceHttpsDisableOption(connId, 0x01);
    sceHttpsDisableOption(connId, 0x02);

    int reqId = sceHttpCreateRequestWithURL(connId, SCE_HTTP_METHOD_POST, url.c_str(), jsonBody.size());
    if (reqId < 0) {
        sceHttpDeleteConnection(connId);
        resp.error = "sceHttpCreateRequestWithURL failed: " + std::to_string(reqId);
        return resp;
    }

    sceHttpsDisableOption(reqId, 0x01);
    sceHttpsDisableOption(reqId, 0x02);
    sceHttpSetSendTimeOut(reqId, 30000000);
    sceHttpSetRecvTimeOut(reqId, 60000000);

    for (const auto& h : headers) {
        std::string k = trimString(h.first);
        std::string v = trimString(h.second);
        if (!k.empty() && !v.empty()) {
            sceHttpAddRequestHeader(reqId, k.c_str(), v.c_str(), SCE_HTTP_HEADER_OVERWRITE);
        }
    }

    int sendRes = sceHttpSendRequest(reqId, jsonBody.empty() ? nullptr : jsonBody.data(), jsonBody.size());
    if (sendRes >= 0) {
        int statusCode = 0;
        sceHttpGetStatusCode(reqId, &statusCode);
        resp.statusCode = statusCode;

        char buf[2048];
        int readBytes = 0;
        while ((readBytes = sceHttpReadData(reqId, buf, sizeof(buf))) > 0) {
            resp.body.append(buf, readBytes);
        }
    } else {
        resp.error = "sceHttpSendRequest failed: " + std::to_string(sendRes);
    }

    sceHttpDeleteRequest(reqId);
    sceHttpDeleteConnection(connId);
    return resp;
#endif
}

HttpResponse HttpClient::get(const std::string& url,
                            const std::vector<std::pair<std::string, std::string>>& headers) {
    AtomicRecursiveGuard lock(g_httpLock);
    HttpResponse resp;

#if defined(_WIN32)
    HINTERNET hSession = WinHttpOpen(L"Luna-Client/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        resp.error = "WinHttpOpen failed";
        return resp;
    }

    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256]{};
    wchar_t urlPath[1024]{};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName) / sizeof(wchar_t);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(wchar_t);

    std::wstring wUrl = toWString(url);
    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        resp.error = "Invalid URL";
        return resp;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        resp.error = "WinHttpConnect failed";
        return resp;
    }

    DWORD reqFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        resp.error = "WinHttpOpenRequest failed";
        return resp;
    }

    std::wstring allHeaders;
    for (const auto& h : headers) {
        allHeaders += toWString(h.first) + L": " + toWString(h.second) + L"\r\n";
    }

    BOOL sendOk = WinHttpSendRequest(hRequest,
                                     allHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : allHeaders.c_str(),
                                     (DWORD)allHeaders.length(),
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (sendOk && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
        resp.statusCode = (int)statusCode;

        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        std::vector<char> buffer;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            buffer.resize(dwSize);
            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                resp.body.append(buffer.data(), dwDownloaded);
            }
        } while (dwSize > 0);
    } else {
        resp.error = "WinHttpSendRequest or ReceiveResponse failed";
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;

#else
    if (!init()) {
        char errBuf[128];
        std::snprintf(errBuf, sizeof(errBuf), "HttpClient init error (pool=%d, ssl=%d, http=%d, tmpl=%d)",
                      g_netPoolId, g_sslCtxId, g_httpCtxId, g_tmplId);
        resp.error = errBuf;
        return resp;
    }

    int connId = sceHttpCreateConnectionWithURL(g_tmplId, url.c_str(), 0);
    if (connId < 0) {
        resp.error = "sceHttpCreateConnectionWithURL failed: " + std::to_string(connId);
        return resp;
    }

    sceHttpsDisableOption(connId, 0x01);
    sceHttpsDisableOption(connId, 0x02);
    sceHttpSetAutoRedirect(connId, 1);

    int reqId = sceHttpCreateRequestWithURL(connId, SCE_HTTP_METHOD_GET, url.c_str(), 0);
    if (reqId < 0) {
        sceHttpDeleteConnection(connId);
        resp.error = "sceHttpCreateRequestWithURL failed: " + std::to_string(reqId);
        return resp;
    }

    sceHttpsDisableOption(reqId, 0x01);
    sceHttpsDisableOption(reqId, 0x02);
    sceHttpSetAutoRedirect(reqId, 1);
    sceHttpSetSendTimeOut(reqId, 30000000);
    sceHttpSetRecvTimeOut(reqId, 60000000);

    for (const auto& h : headers) {
        std::string k = trimString(h.first);
        std::string v = trimString(h.second);
        if (!k.empty() && !v.empty()) {
            sceHttpAddRequestHeader(reqId, k.c_str(), v.c_str(), SCE_HTTP_HEADER_OVERWRITE);
        }
    }

    int sendRes = sceHttpSendRequest(reqId, nullptr, 0);
    if (sendRes >= 0) {
        int statusCode = 0;
        sceHttpGetStatusCode(reqId, &statusCode);
        resp.statusCode = statusCode;

        char buf[16384];
        int readBytes = 0;
        while ((readBytes = sceHttpReadData(reqId, buf, sizeof(buf))) > 0) {
            resp.body.append(buf, readBytes);
        }
    } else {
        resp.error = "sceHttpSendRequest failed: " + std::to_string(sendRes);
    }

    sceHttpDeleteRequest(reqId);
    sceHttpDeleteConnection(connId);
    return resp;
#endif
}

} // namespace luna::net
