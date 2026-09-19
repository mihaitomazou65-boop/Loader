#pragma once

#include <optional>
#include <string>

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string error;
};

class HttpClient {
public:
    static HttpResponse request(
        const std::wstring& method,
        const std::wstring& path,
        const std::string& jsonBody = {},
        const std::string& bearerToken = {});
};
