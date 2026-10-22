#pragma once

#include <string>
#include <vector>
#include <utility>

namespace luna::net {

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::string error;

    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

class HttpClient {
public:
    static bool init();
    static void shutdown();

    static HttpResponse post(const std::string& url,
                             const std::string& jsonBody,
                             const std::vector<std::pair<std::string, std::string>>& headers = {});

    static HttpResponse get(const std::string& url,
                            const std::vector<std::pair<std::string, std::string>>& headers = {});
};

} // namespace luna::net
