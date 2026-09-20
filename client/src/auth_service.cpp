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

float jsonNum(const json& j, const char* key, float fallback = 0.5f) {
    if (!j.contains(key))
        return fallback;
    const auto& v = j[key];
    if (v.is_number())
        return (float)v.get<double>();
    if (v.is_string()) {
        try { return std::stof(v.get<std::string>()); } catch (...) { return fallback; }
    }
    return fallback;
}

std::string jsonTextAny(const json& j, const char* a, const char* b = nullptr) {
    std::string s = jsonText(j, a);
    if (s.empty() && b)
        s = jsonText(j, b);
    return s;
}

void dedupeProducts(std::vector<ProductEntitlement>& list) {
    std::vector<ProductEntitlement> out;
    out.reserve(list.size());
    for (const ProductEntitlement& p : list) {
        if (p.product.empty())
            continue;
        bool exists = false;
        for (const ProductEntitlement& e : out) {
            if (e.product == p.product) {
                exists = true;
                break;
            }
        }
        if (!exists)
            out.push_back(p);
    }
    list.swap(out);
}

ProductEntitlement parseProductEntitlement(const json& u) {
    ProductEntitlement p;
    p.product = jsonTextAny(u, "product", OBF("product"));
    p.expires = jsonTextAny(u, "expires", OBF("expires"));
    p.fileName = jsonTextAny(u, "file_name", OBF("file_name"));
    p.fileVersion = jsonTextAny(u, "file_version", OBF("file_version"));
    p.thumbVersion = jsonTextAny(u, "thumb_version", OBF("thumb_version"));
    p.thumbFx = jsonNum(u, "thumb_fx", jsonNum(u, OBF("thumb_fx"), 0.5f));
    p.thumbFy = jsonNum(u, "thumb_fy", jsonNum(u, OBF("thumb_fy"), 0.5f));
    auto readLife = [&](const char* k) {
        if (u.contains(k) && u[k].is_boolean())
            p.lifetime = u[k].get<bool>();
    };
    readLife("lifetime");
    readLife(OBF("lifetime"));
    return p;
}

json findProductsArray(const json& u) {
    auto tryGet = [&](const char* key) -> json {
        if (!u.contains(key))
            return json();
        const json& v = u[key];
        if (v.is_array())
            return v;
        if (v.is_object()) {
            json arr = json::array();
            for (auto it = v.begin(); it != v.end(); ++it) {
                if (it.value().is_object())
                    arr.push_back(it.value());
            }
            return arr;
        }
        if (v.is_string()) {
            json parsed = json::parse(v.get<std::string>(), nullptr, false);
            if (!parsed.is_discarded() && parsed.is_array())
                return parsed;
        }
        return json();
    };
    json arr = tryGet("products");
    if (arr.is_array())
        return arr;
    arr = tryGet(OBF("products"));
    if (arr.is_array())
        return arr;
    for (auto it = u.begin(); it != u.end(); ++it) {
        if (!it.value().is_array() || it.value().empty())
            continue;
        if (it.value().front().is_object() && (it.value().front().contains("product") || it.value().front().contains("name")))
            return it.value();
    }
    return json::array();
}

void addProductName(std::vector<ProductEntitlement>& list, const std::string& name) {
    if (name.empty())
        return;
    for (const ProductEntitlement& p : list) {
        if (p.product == name)
            return;
    }
    ProductEntitlement p;
    p.product = name;
    list.push_back(std::move(p));
}

void takeProductsJson(const json& arr, std::vector<ProductEntitlement>& out) {
    if (!arr.is_array())
        return;
    for (size_t i = 0; i < arr.size(); ++i) {
        const json item = arr.at(i);
        if (item.is_string()) {
            addProductName(out, item.get<std::string>());
            continue;
        }
        if (!item.is_object())
            continue;
        ProductEntitlement p = parseProductEntitlement(item);
        if (p.product.empty())
            p.product = jsonTextAny(item, "name", OBF("name"));
        if (p.product.empty())
            continue;
        bool exists = false;
        for (ProductEntitlement& e : out) {
            if (e.product == p.product) {
                e = p;
                exists = true;
                break;
            }
        }
        if (!exists)
            out.push_back(std::move(p));
    }
}

void readProductsArray(const json& u, AuthUser& user) {
    const bool listed = u.contains("products") || u.contains("product_names") || u.contains("product_list");
    if (listed)
        user.productsFromServer = true;
    std::vector<ProductEntitlement> got;
    takeProductsJson(findProductsArray(u), got);
    if (u.contains("product_names"))
        takeProductsJson(u["product_names"], got);
    if (u.contains("product_list"))
        takeProductsJson(u["product_list"], got);
    if (listed || !got.empty())
        user.products = std::move(got);
    dedupeProducts(user.products);
}

void applyPrimaryFromProducts(AuthUser& user) {
    if (user.products.empty() && !user.productsFromServer && !user.product.empty()) {
        ProductEntitlement p;
        p.product = user.product;
        p.lifetime = user.lifetime;
        p.expires = user.expires;
        p.fileName = user.fileName;
        p.fileVersion = user.fileVersion;
        p.thumbVersion = user.thumbVersion;
        p.thumbFx = user.thumbFx;
        p.thumbFy = user.thumbFy;
        user.products.push_back(std::move(p));
    }
    dedupeProducts(user.products);
    if (user.products.empty())
        return;
    const ProductEntitlement& p = user.products.front();
    user.product = p.product;
    user.lifetime = p.lifetime;
    user.expires = p.expires;
    user.fileName = p.fileName;
    user.fileVersion = p.fileVersion;
    user.thumbVersion = p.thumbVersion;
    user.thumbFx = p.thumbFx;
    user.thumbFy = p.thumbFy;
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
                out.user.id = jsonTextAny(u, "id", OBF("id"));
                out.user.email = jsonTextAny(u, "name", OBF("name"));
                if (out.user.email.empty())
                    out.user.email = jsonTextAny(u, "email", OBF("email"));
                out.user.product = jsonTextAny(u, "product", OBF("product"));
                out.user.expires = jsonTextAny(u, "expires", OBF("expires"));
                out.user.fileName = jsonTextAny(u, "file_name", OBF("file_name"));
                out.user.fileVersion = jsonTextAny(u, "file_version", OBF("file_version"));
                out.user.thumbVersion = jsonTextAny(u, "thumb_version", OBF("thumb_version"));
                out.user.thumbFx = jsonNum(u, "thumb_fx", jsonNum(u, OBF("thumb_fx"), 0.5f));
                out.user.thumbFy = jsonNum(u, "thumb_fy", jsonNum(u, OBF("thumb_fy"), 0.5f));
                if (u.contains("lifetime") && u["lifetime"].is_boolean())
                    out.user.lifetime = u["lifetime"].get<bool>();
                else if (u.contains(OBF("lifetime")) && u[OBF("lifetime")].is_boolean())
                    out.user.lifetime = u[OBF("lifetime")].get<bool>();
                readProductsArray(u, out.user);
                if (out.user.products.empty())
                    readProductsArray(j, out.user);
                applyPrimaryFromProducts(out.user);
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
        json products = json::array();
        for (const ProductEntitlement& p : user.products) {
            json item = json::object();
            item["product"] = p.product;
            item["lifetime"] = p.lifetime;
            item["expires"] = p.expires;
            item["file_name"] = p.fileName;
            item["file_version"] = p.fileVersion;
            item["thumb_version"] = p.thumbVersion;
            item["thumb_fx"] = p.thumbFx;
            item["thumb_fy"] = p.thumbFy;
            products.push_back(std::move(item));
        }
        json names = json::array();
        for (const ProductEntitlement& p : user.products) {
            if (!p.product.empty())
                names.push_back(p.product);
        }
        json j = json::object();
        j["token"] = token;
        j["id"] = user.id;
        j["email"] = user.email;
        j["product"] = user.product;
        j["lifetime"] = user.lifetime;
        j["expires"] = user.expires;
        j["file_name"] = user.fileName;
        j["file_version"] = user.fileVersion;
        j["thumb_version"] = user.thumbVersion;
        j["thumb_fx"] = user.thumbFx;
        j["thumb_fy"] = user.thumbFy;
        j["products"] = products;
        j["product_names"] = names;
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
        tokenOut = jsonTextAny(j, "token", OBF("token"));
        userOut.id = jsonTextAny(j, "id", OBF("id"));
        userOut.email = jsonTextAny(j, "email", OBF("email"));
        userOut.product = jsonTextAny(j, "product", OBF("product"));
        userOut.expires = jsonTextAny(j, "expires", OBF("expires"));
        userOut.fileName = jsonTextAny(j, "file_name", OBF("file_name"));
        userOut.fileVersion = jsonTextAny(j, "file_version", OBF("file_version"));
        userOut.thumbVersion = jsonTextAny(j, "thumb_version", OBF("thumb_version"));
        userOut.thumbFx = jsonNum(j, "thumb_fx", jsonNum(j, OBF("thumb_fx"), 0.5f));
        userOut.thumbFy = jsonNum(j, "thumb_fy", jsonNum(j, OBF("thumb_fy"), 0.5f));
        if (j.contains("lifetime") && j["lifetime"].is_boolean())
            userOut.lifetime = j["lifetime"].get<bool>();
        else if (j.contains(OBF("lifetime")) && j[OBF("lifetime")].is_boolean())
            userOut.lifetime = j[OBF("lifetime")].get<bool>();
        readProductsArray(j, userOut);
        applyPrimaryFromProducts(userOut);
        return !tokenOut.empty() || !userOut.products.empty();
    } catch (...) {
        return false;
    }
}

void AuthService::clearSession() {
    std::error_code ec;
    fs::remove(sessionPath(), ec);
    fs::remove(legacySessionPath(), ec);
}
