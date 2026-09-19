#pragma once

#include <optional>
#include <string>

struct AuthUser {
    std::string id;
    std::string email;
};

struct AuthResult {
    bool ok = false;
    std::string message;
    std::string token;
    AuthUser user;
};

class AuthService {
public:
    AuthResult signup(const std::string& email, const std::string& password);
    AuthResult login(const std::string& email, const std::string& password);
    AuthResult me(const std::string& token);

    bool saveSession(const std::string& token, const AuthUser& user);
    bool loadSession(std::string& tokenOut, AuthUser& userOut);
    void clearSession();

private:
    AuthResult parseAuthResponse(const struct HttpResponse& resp);
};
