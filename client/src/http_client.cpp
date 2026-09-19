#include "http_client.hpp"
#include "config.hpp"

#include <windows.h>
#include <winhttp.h>

#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

struct UrlParts {
    bool https = false;
    std::wstring host;
    INTERNET_PORT port = 0;
    std::wstring path;
};

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::string wideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), len, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

bool parseBaseUrl(const std::string& url, UrlParts& out) {
    std::string u = url;
    if (u.rfind("https://", 0) == 0) {
        out.https = true;
        u = u.substr(8);
        out.port = INTERNET_DEFAULT_HTTPS_PORT;
    } else if (u.rfind("http://", 0) == 0) {
        out.https = false;
        u = u.substr(7);
        out.port = INTERNET_DEFAULT_HTTP_PORT;
    } else {
        return false;
    }

    const size_t slash = u.find('/');
    const std::string hostPort = slash == std::string::npos ? u : u.substr(0, slash);
    const std::string pathPart = slash == std::string::npos ? "/" : u.substr(slash);

    const size_t colon = hostPort.find(':');
    if (colon != std::string::npos) {
        out.host = utf8ToWide(hostPort.substr(0, colon));
        out.port = static_cast<INTERNET_PORT>(std::stoi(hostPort.substr(colon + 1)));
    } else {
        out.host = utf8ToWide(hostPort);
    }

    out.path = utf8ToWide(pathPart);
    return !out.host.empty();
}

} // namespace

HttpResponse HttpClient::request(
    const std::wstring& method,
    const std::wstring& path,
    const std::string& jsonBody,
    const std::string& bearerToken) {

    HttpResponse result;
    UrlParts base;
    if (!parseBaseUrl(LOADER_API_BASE_URL, base)) {
        result.error = "Invalid LOADER_API_BASE_URL";
        return result;
    }

    HINTERNET session = WinHttpOpen(L"Loader/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.error = "WinHttpOpen failed";
        return result;
    }
    WinHttpSetTimeouts(session, 4000, 4000, 8000, 8000);
    DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls));

    HINTERNET connect = WinHttpConnect(session, base.host.c_str(), base.port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        result.error = "WinHttpConnect failed";
        return result;
    }

    std::wstring fullPath = base.path;
    if (!fullPath.empty() && fullPath.back() == L'/') fullPath.pop_back();
    fullPath += path;

    HINTERNET request = WinHttpOpenRequest(connect, method.c_str(), fullPath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        base.https ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        result.error = "WinHttpOpenRequest failed";
        return result;
    }

        std::wstring headerBlock = L"Content-Type: application/json\r\n";
    if (!bearerToken.empty()) {
        headerBlock += L"Authorization: Bearer " + utf8ToWide(bearerToken) + L"\r\n";
    }
    const wchar_t* headers = headerBlock.c_str();

    const BOOL sent = WinHttpSendRequest(
        request,
        headers,
        static_cast<DWORD>(-1L),
        jsonBody.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)jsonBody.data(),
        static_cast<DWORD>(jsonBody.size()),
        static_cast<DWORD>(jsonBody.size()),
        0);

    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        result.error = "Request failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.status = static_cast<int>(status);

    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
            break;
        std::vector<char> chunk(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read))
            break;
        body.append(chunk.data(), read);
    }

    result.body = std::move(body);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}
