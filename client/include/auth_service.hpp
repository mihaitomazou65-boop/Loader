#pragma once

#include <optional>
#include <string>

struct AuthUser {
    std::string id;
    std::string email;
    std::string product;
    bool lifetime = false;
    std::string expires;
    std::string fileName;
    std::string fileVersion;
    std::string thumbVersion;
    float thumbFx = 0.5f;
    float thumbFy = 0.5f;
};

struct AuthResult {
    bool ok = false;
    bool liveSync = false;
    std::string message;
    std::string token;
    AuthUser user;
};

class AuthService {
public:
    AuthResult signup(const std::string& email, const std::string& password);
    AuthResult login(const std::string& email, const std::string& password);
    AuthResult me(const std::string& token);
    AuthResult redeem(const std::string& token, const std::string& key);

    bool saveSession(const std::string& token, const AuthUser& user);
    bool loadSession(std::string& tokenOut, AuthUser& userOut);
    void clearSession();

private:
    AuthResult parseAuthResponse(const struct HttpResponse& resp);
};
