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

std::string jsonText(const json& j, const char* key) {
    if (!j.contains(key))
        return {};
    const auto& v = j[key];
    if (v.is_string())
        return v.get<std::string>();
    if (v.is_number())
        return v.dump();
    return {};
}

json parseBody(const std::string& body) {
    if (body.empty())
        return json::object();
    if (body[0] == '<' || body.find("Application loading") != std::string::npos)
        return json::object();
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return json::object();
    return j;
}

} // namespace

AuthResult AuthService::parseAuthResponse(const HttpResponse& resp) {
    AuthResult out;
    try {
        if (!resp.error.empty() && resp.body.empty()) {
            out.message = resp.error;
            return out;
        }

        const json j = parseBody(resp.body);
        out.message = jsonText(j, "message");

        if (resp.status >= 200 && resp.status < 300) {
            out.token = jsonText(j, "token");
            if (j.contains("user") && j["user"].is_object()) {
                const json& u = j["user"];
                out.user.id = jsonText(u, "id");
                out.user.email = jsonText(u, "name");
                if (out.user.email.empty())
                    out.user.email = jsonText(u, "email");
            }
            out.ok = !out.token.empty();
            if (out.ok)
                out.message.clear();
            else if (out.message.empty())
                out.message = "Login failed";
            return out;
        }

        if (out.message.empty()) {
            if (resp.status == 409)
                out.message = "Name already taken";
            else if (resp.status == 401)
                out.message = "Wrong name or password";
            else if (resp.status == 0)
                out.message = resp.error.empty() ? "Can't reach server" : resp.error;
            else
                out.message = "Request failed";
        }
        return out;
    } catch (...) {
        out.ok = false;
        out.message = "Wrong name or password";
        return out;
    }
}

AuthResult AuthService::signup(const std::string& name, const std::string& password) {
    try {
        json body = { {"name", name}, {"email", name}, {"password", password} };
        return parseAuthResponse(HttpClient::request(L"POST", L"/auth/signup", body.dump()));
    } catch (...) {
        AuthResult out;
        out.message = "Sign up failed";
        return out;
    }
}

AuthResult AuthService::login(const std::string& name, const std::string& password) {
    try {
        json body = { {"name", name}, {"email", name}, {"password", password} };
        return parseAuthResponse(HttpClient::request(L"POST", L"/auth/login", body.dump()));
    } catch (...) {
        AuthResult out;
        out.message = "Wrong name or password";
        return out;
    }
}

AuthResult AuthService::me(const std::string& token) {
    AuthResult out;
    try {
        const HttpResponse resp = HttpClient::request(L"GET", L"/auth/me", {}, token);
        if (resp.status >= 200 && resp.status < 300) {
            const json j = parseBody(resp.body);
            if (j.contains("user") && j["user"].is_object()) {
                out.user.id = jsonText(j["user"], "id");
                out.user.email = jsonText(j["user"], "name");
                if (out.user.email.empty())
                    out.user.email = jsonText(j["user"], "email");
                out.ok = !out.user.email.empty();
            }
            return out;
        }
        return parseAuthResponse(resp);
    } catch (...) {
        out.message = "Session check failed";
        return out;
    }
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
        if (!j.is_object())
            return false;
        tokenOut = jsonText(j, "token");
        userOut.id = jsonText(j, "id");
        userOut.email = jsonText(j, "email");
        return !tokenOut.empty();
    } catch (...) {
        return false;
    }
}

void AuthService::clearSession() {
    std::error_code ec;
    fs::remove(sessionPath(), ec);
}
