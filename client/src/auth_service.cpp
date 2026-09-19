#include "auth_service.hpp"
#include "http_client.hpp"
#include "obfuscate.hpp"
#include "session_crypto.hpp"

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
    fs::path dir = fs::path(buf) / OBFW(L"Loader");
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / OBFW(L"session.dat");
}

fs::path legacySessionPath() {
    wchar_t buf[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf);
    return fs::path(buf) / OBFW(L"Loader") / OBFW(L"session.json");
}

bool parseSessionJson(const std::string& text, json& jOut) {
    if (text.empty())
        return false;
    jOut = json::parse(text, nullptr, false);
    return !jOut.is_discarded() && jOut.is_object();
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
    if (body[0] == '<' || body.find(OBF("Application loading")) != std::string::npos)
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
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, OBFW(L"SOFTWARE\\Microsoft\\Cryptography"), 0, KEY_READ | KEY_WOW64_64KEY,
            &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, OBFW(L"MachineGuid"), nullptr, nullptr, (LPBYTE)guid, &guidSize);
        RegCloseKey(key);
    }
    blob += OBF("mg=");
    blob += wideToUtf8Local(guid);
    blob += OBF(";");

    wchar_t computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD computerLen = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computer, &computerLen);
    blob += OBF("cn=");
    blob += wideToUtf8Local(computer);
    blob += OBF(";");

    wchar_t user[UNLEN + 1]{};
    DWORD userLen = UNLEN + 1;
    GetUserNameW(user, &userLen);
    blob += OBF("un=");
    blob += wideToUtf8Local(user);
    blob += OBF(";");

    DWORD serial = 0;
    GetVolumeInformationW(OBFW(L"C:\\"), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    blob += OBF("vs=");
    blob += std::to_string(serial);
    blob += OBF(";");

    ULONG bufLen = 0;
    GetAdaptersInfo(nullptr, &bufLen);
    if (bufLen) {
        std::vector<unsigned char> buf(bufLen);
        if (GetAdaptersInfo(reinterpret_cast<PIP_ADAPTER_INFO>(buf.data()), &bufLen) == NO_ERROR) {
            auto* adp = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
            int n = 0;
            while (adp && n < 8) {
                blob += OBF("mac=");
                for (UINT i = 0; i < adp->AddressLength; ++i) {
                    char hex[8];
                    sprintf_s(hex, "%02x", adp->Address[i]);
                    blob += hex;
                }
                blob += OBF(";");
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
        out.message = jsonText(j, OBF("message"));

        if (resp.status >= 200 && resp.status < 300) {
            out.token = jsonText(j, OBF("token"));
            if (j.contains(OBF("user")) && j[OBF("user")].is_object()) {
                const json& u = j[OBF("user")];
                out.user.id = jsonText(u, OBF("id"));
                out.user.email = jsonText(u, OBF("name"));
                if (out.user.email.empty())
                    out.user.email = jsonText(u, OBF("email"));
                out.user.product = jsonText(u, OBF("product"));
                out.user.expires = jsonText(u, OBF("expires"));
                out.user.fileName = jsonText(u, OBF("file_name"));
                out.user.fileVersion = jsonText(u, OBF("file_version"));
                out.user.thumbVersion = jsonText(u, OBF("thumb_version"));
                if (u.contains(OBF("lifetime")) && u[OBF("lifetime")].is_boolean())
                    out.user.lifetime = u[OBF("lifetime")].get<bool>();
            }
            out.ok = !out.token.empty() || !out.user.id.empty() || !out.user.email.empty()
                || (j.contains(OBF("ok")) && j[OBF("ok")].is_boolean() && j[OBF("ok")].get<bool>());
            if (out.ok && !out.token.empty())
                out.message.clear();
            else if (!out.ok && out.message.empty())
                out.message = OBF("Login failed");
            return out;
        }

        if (out.message.empty()) {
            if (resp.status == 409)
                out.message = OBF("This key has already been used");
            else if (resp.status == 400)
                out.message = OBF("Invalid key");
            else if (resp.status == 403)
                out.message = OBF("Account locked");
            else if (resp.status == 404)
                out.message = OBF("Invalid key");
            else if (resp.status == 401)
                out.message = OBF("Login expired, sign in again");
            else if (resp.status == 429)
                out.message = OBF("Too many tries, wait a bit");
            else if (resp.status == 0)
                out.message = resp.error.empty() ? OBF("Can't reach server") : resp.error;
            else
                out.message = resp.error.empty() ? OBF("Can't redeem this key") : resp.error;
        }
        return out;
    } catch (...) {
        out.ok = false;
        out.message = OBF("Wrong name or password");
        return out;
    }
}

AuthResult AuthService::signup(const std::string& name, const std::string& password) {
    try {
        json body = {
            {OBF("name"), name},
            {OBF("email"), name},
            {OBF("password"), password},
            {OBF("hwid"), deviceHwid()}};
        return parseAuthResponse(HttpClient::request(OBFW(L"POST"), OBFW(L"/auth/signup"), body.dump()));
    } catch (...) {
        AuthResult out;
        out.message = OBF("Sign up failed");
        return out;
    }
}

AuthResult AuthService::login(const std::string& name, const std::string& password) {
    try {
        json body = {
            {OBF("name"), name},
            {OBF("email"), name},
            {OBF("password"), password},
            {OBF("hwid"), deviceHwid()}};
        return parseAuthResponse(HttpClient::request(OBFW(L"POST"), OBFW(L"/auth/login"), body.dump()));
    } catch (...) {
        AuthResult out;
        out.message = OBF("Wrong name or password");
        return out;
    }
}

AuthResult AuthService::redeem(const std::string& token, const std::string& key) {
    try {
        std::string cleaned;
        cleaned.reserve(key.size());
        for (unsigned char c : key) {
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
                continue;
            if (c >= 'a' && c <= 'z')
                cleaned.push_back(static_cast<char>(c - 32));
            else
                cleaned.push_back(static_cast<char>(c));
        }
        json body = { {OBF("key"), cleaned} };
        return parseAuthResponse(HttpClient::request(OBFW(L"POST"), OBFW(L"/auth/redeem"), body.dump(), token));
    } catch (...) {
        AuthResult out;
        out.message = OBF("Redeem failed");
        return out;
    }
}

AuthResult AuthService::me(const std::string& token) {
    try {
        const HttpResponse resp = HttpClient::request(OBFW(L"GET"), OBFW(L"/auth/me"), {}, token);
        return parseAuthResponse(resp);
    } catch (...) {
        AuthResult out;
        out.message = OBF("Session check failed");
        return out;
    }
}

bool AuthService::saveSession(const std::string& token, const AuthUser& user) {
    try {
        json j = {
            {OBF("token"), token},
            {OBF("id"), user.id},
            {OBF("email"), user.email},
            {OBF("product"), user.product},
            {OBF("lifetime"), user.lifetime},
            {OBF("expires"), user.expires},
            {OBF("file_name"), user.fileName},
            {OBF("file_version"), user.fileVersion},
            {OBF("thumb_version"), user.thumbVersion}};
        return session_crypto::writeEncryptedFile(sessionPath(), j.dump());
    } catch (...) {
        return false;
    }
}

bool AuthService::loadSession(std::string& tokenOut, AuthUser& userOut) {
    try {
        json j;
        std::string plain;
        const fs::path path = sessionPath();
        if (session_crypto::readEncryptedFile(path, plain) && parseSessionJson(plain, j)) {
            // ok
        } else {
            std::ifstream legacy(legacySessionPath());
            if (!legacy)
                return false;
            std::string legacyText((std::istreambuf_iterator<char>(legacy)), std::istreambuf_iterator<char>());
            if (!parseSessionJson(legacyText, j))
                return false;
            session_crypto::writeEncryptedFile(path, legacyText);
            fs::remove(legacySessionPath());
        }
        tokenOut = jsonText(j, OBF("token"));
        userOut.id = jsonText(j, OBF("id"));
        userOut.email = jsonText(j, OBF("email"));
        userOut.product = jsonText(j, OBF("product"));
        userOut.expires = jsonText(j, OBF("expires"));
        userOut.fileName = jsonText(j, OBF("file_name"));
        userOut.fileVersion = jsonText(j, OBF("file_version"));
        userOut.thumbVersion = jsonText(j, OBF("thumb_version"));
        if (j.contains(OBF("lifetime")) && j[OBF("lifetime")].is_boolean())
            userOut.lifetime = j[OBF("lifetime")].get<bool>();
        return !tokenOut.empty();
    } catch (...) {
        return false;
    }
}

void AuthService::clearSession() {
    std::error_code ec;
    fs::remove(sessionPath(), ec);
    fs::remove(legacySessionPath(), ec);
}
