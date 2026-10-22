#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// SceSysmodule
#define SCE_SYSMODULE_NET  0x000F
#define SCE_SYSMODULE_HTTP 0x0010
#define SCE_SYSMODULE_SSL  0x0011
int sceSysmoduleLoadModule(uint32_t id);
int sceSysmoduleUnloadModule(uint32_t id);

// SceNet
int sceNetInit(void);
int sceNetTerm(void);
int sceNetPoolCreate(const char* name, int size, int flags);
int sceNetPoolDestroy(int memid);

// SceSsl
int sceSslInit(size_t poolSize);
int sceSslTerm(int sslCtxId);

// SceHttps options
int sceHttpsDisableOption(int id, unsigned int flags);
int sceHttpsDisableOptionPrivate(int id, unsigned int flags);

// SceHttp constants
#define SCE_HTTP_VERSION_1_0 1
#define SCE_HTTP_VERSION_1_1 2

#define SCE_HTTP_METHOD_GET     0
#define SCE_HTTP_METHOD_POST    1
#define SCE_HTTP_METHOD_HEAD    2
#define SCE_HTTP_METHOD_OPTIONS 3
#define SCE_HTTP_METHOD_PUT     4
#define SCE_HTTP_METHOD_DELETE  5
#define SCE_HTTP_METHOD_TRACE   6
#define SCE_HTTP_METHOD_CONNECT 7

#define SCE_HTTP_HEADER_ADD       0
#define SCE_HTTP_HEADER_OVERWRITE 1

// SceHttp
int sceHttpInit(int netPoolId, int sslCtxId, size_t poolSize);
int sceHttpTerm(int httpCtxId);

int sceHttpCreateTemplate(int httpCtxId, const char* userAgent, int httpVersion, int autoRedirect);
int sceHttpDeleteTemplate(int tmplId);

int sceHttpCreateConnectionWithURL(int tmplId, const char* url, int isKeepAlive);
int sceHttpDeleteConnection(int connId);

int sceHttpCreateRequestWithURL(int connId, int method, const char* url, uint64_t contentLength);
int sceHttpDeleteRequest(int reqId);

int sceHttpAddRequestHeader(int reqId, const char* name, const char* value, int mode);
int sceHttpSendRequest(int reqId, const void* postData, size_t size);
int sceHttpGetStatusCode(int reqId, int* statusCode);
int sceHttpReadData(int reqId, void* data, unsigned int size);
int sceHttpGetResponseContentLength(int reqId, int* result, uint64_t* contentLength);

int sceHttpSetAutoRedirect(int id, int enable);
int sceHttpSetResolveTimeOut(int id, unsigned int timeout);
int sceHttpSetResolveRetry(int id, int retry);
int sceHttpSetConnectTimeOut(int id, unsigned int timeout);
int sceHttpSetSendTimeOut(int id, unsigned int timeout);
int sceHttpSetRecvTimeOut(int id, unsigned int timeout);

#ifdef __cplusplus
}
#endif
