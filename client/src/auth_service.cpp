#include "auth_service.hpp"
#include "http_client.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <shlobj.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path sessionPath() {
    wchar_t buf[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf);
    fs::path dir = fs::path(buf) / L"Loader";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / L"session.json";
}

} // namespace

AuthResult AuthService::parseAuthResponse(const HttpResponse& resp) {
    AuthResult out;
    if (resp.error.empty() == false && resp.body.empty()) {
        out.message = resp.error;
        return out;
    }

    try {
        const json j = json::parse(resp.body.empty() ? "{}" : resp.body);
        if (j.contains("message") && j["message"].is_string())
            out.message = j["message"].get<std::string>();
    } catch (...) {
        if (resp.status == 0)
            out.message = "Network error";
        else
            out.message = "Unexpected server response";
        return out;
    }

    if (resp.status >= 200 && resp.status < 300) {
        try {
            const json j = json::parse(resp.body);
            out.token = j.value("token", "");
            if (j.contains("user") && j["user"].is_object()) {
                out.user.id = j["user"].value("id", "");
                out.user.email = j["user"].value("email", "");
            }
            out.ok = !out.token.empty();
            if (out.ok && out.message.empty())
                out.message = "Success";
        } catch (...) {
            out.message = "Invalid response JSON";
        }
        return out;
    }

    if (out.message.empty())
        out.message = "Request failed";
    return out;
}

AuthResult AuthService::signup(const std::string& email, const std::string& password) {
    json body = { {"email", email}, {"password", password} };
    const HttpResponse resp = HttpClient::request(L"POST", L"/auth/signup", body.dump());
    return parseAuthResponse(resp);
}

AuthResult AuthService::login(const std::string& email, const std::string& password) {
    json body = { {"email", email}, {"password", password} };
    const HttpResponse resp = HttpClient::request(L"POST", L"/auth/login", body.dump());
    return parseAuthResponse(resp);
}

AuthResult AuthService::me(const std::string& token) {
    AuthResult out;
    const HttpResponse resp = HttpClient::request(L"GET", L"/auth/me", {}, token);
    if (resp.status >= 200 && resp.status < 300) {
        try {
            const json j = json::parse(resp.body);
            if (j.contains("user")) {
                out.user.id = j["user"].value("id", "");
                out.user.email = j["user"].value("email", "");
                out.ok = !out.user.email.empty();
                out.message = "Session valid";
            }
        } catch (...) {
            out.message = "Invalid session response";
        }
        return out;
    }
    return parseAuthResponse(resp);
}

bool AuthService::saveSession(const std::string& token, const AuthUser& user) {
    try {
        json j = { {"token", token}, {"id", user.id}, {"email", user.email} };
        std::ofstream f(sessionPath(), std::ios::trunc);
        f << j.dump(2);
        return f.good();
    } catch (...) {
        return false;
    }
}

bool AuthService::loadSession(std::string& tokenOut, AuthUser& userOut) {
    try {
        std::ifstream f(sessionPath());
        if (!f)
            return false;
        json j;
        f >> j;
        tokenOut = j.value("token", "");
        userOut.id = j.value("id", "");
        userOut.email = j.value("email", "");
        return !tokenOut.empty();
    } catch (...) {
        return false;
    }
}

void AuthService::clearSession() {
    std::error_code ec;
    fs::remove(sessionPath(), ec);
}
