#include "auth_service.hpp"
#include "http_client.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <shlobj.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <lmcons.h>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "bcrypt.lib")

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

std::string toHex(const unsigned char* data, size_t len) {
    static const char* k = "0123456789abcdef";
    std::string out;
    out.resize(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = k[data[i] >> 4];
        out[i * 2 + 1] = k[data[i] & 0xf];
    }
    return out;
}

std::string sha256Hex(const std::string& input) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objLen = 0, dataLen = 0, hashLen = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &dataLen, 0);
    BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &dataLen, 0);
    std::vector<UCHAR> obj(objLen), digest(hashLen);
    if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    BCryptHashData(hash, (PUCHAR)input.data(), (ULONG)input.size(), 0);
    BCryptFinishHash(hash, digest.data(), hashLen, 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    return toHex(digest.data(), digest.size());
}

std::string wideToUtf8Local(const std::wstring& s) {
    if (s.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, out.data(), len, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::string collectTraces() {
    std::string blob;

    wchar_t guid[256]{};
    DWORD guidSize = sizeof(guid);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"MachineGuid", nullptr, nullptr, (LPBYTE)guid, &guidSize);
        RegCloseKey(key);
    }
    blob += "mg=";
    blob += wideToUtf8Local(guid);
    blob += ";";

    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD computerLen = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer, &computerLen);
    blob += "cn=";
    blob += wideToUtf8Local(computer);
    blob += ";";

    wchar_t user[UNLEN + 1]{};
    DWORD userLen = UNLEN + 1;
    GetUserNameW(user, &userLen);
    blob += "un=";
    blob += wideToUtf8Local(user);
    blob += ";";

    DWORD serial = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    blob += "vs=";
    blob += std::to_string(serial);
    blob += ";";

    ULONG bufLen = 0;
    GetAdaptersInfo(nullptr, &bufLen);
    if (bufLen) {
        std::vector<unsigned char> buf(bufLen);
        if (GetAdaptersInfo(reinterpret_cast<PIP_ADAPTER_INFO>(buf.data()), &bufLen) == NO_ERROR) {
            auto* adp = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
            int n = 0;
            while (adp && n < 8) {
                blob += "mac=";
                for (UINT i = 0; i < adp->AddressLength; ++i) {
                    char hex[8];
                    sprintf_s(hex, "%02x", adp->Address[i]);
                    blob += hex;
                }
                blob += ";";
                adp = adp->Next;
                ++n;
            }
        }
    }
    return blob;
}

std::string deviceHwid() {
    const std::string hex = sha256Hex(collectTraces());
    return hex.empty() ? std::string(64, '0') : hex;
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
            else if (resp.status == 403)
                out.message = out.message.empty() ? "Account locked to this device" : out.message;
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
        json body = { {"name", name}, {"email", name}, {"password", password}, {"hwid", deviceHwid()} };
        return parseAuthResponse(HttpClient::request(L"POST", L"/auth/signup", body.dump()));
    } catch (...) {
        AuthResult out;
        out.message = "Sign up failed";
        return out;
    }
}

AuthResult AuthService::login(const std::string& name, const std::string& password) {
    try {
        json body = { {"name", name}, {"email", name}, {"password", password}, {"hwid", deviceHwid()} };
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
