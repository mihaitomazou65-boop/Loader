#define IMGUI_DEFINE_MATH_OPERATORS
#include "auth_screen.hpp"
#include "app_window.hpp"
#include "config.hpp"
#include "auth_service.hpp"
#include "custom_widgets.hpp"
#include "imgui_settings.h"
#include "font.h"
#include "font_defines.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace loader_ui {
namespace {

enum class AuthMode { Login, SignUp };
enum class View { Form, LoggedIn };

AuthService g_auth;
View g_view = View::Form;
AuthMode g_mode = AuthMode::Login;
std::atomic<bool> g_busy{false};
bool g_wantSubmit = false;
bool g_error = false;
char g_name[64]{};
char g_password[128]{};
char g_status[256]{};
std::string g_token;
AuthUser g_user;
bool g_sessionChecked = false;
bool g_fontsReady = false;
std::mutex g_resultMu;
bool g_gotResult = false;
AuthResult g_pending;

constexpr float kRound = 12.f;
constexpr float kFieldH = 34.f;

ImU32 col32(const ImColor& c) {
    return ImGui::GetColorU32(utils::ImColorToImVec4(c));
}

void setStatus(const char* msg, bool error) {
    g_error = error;
    if (!msg) {
        g_status[0] = 0;
        g_error = false;
        return;
    }
    strncpy_s(g_status, msg, _TRUNCATE);
}

void initFonts() {
    if (g_fontsReady)
        return;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig cfg{};
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    font::esp_font = io.Fonts->AddFontFromMemoryTTF(PoppinsSemiBold, (int)sizeof(PoppinsSemiBold), 16.f, &cfg, io.Fonts->GetGlyphRangesDefault());
    font::s_inter_semibold = io.Fonts->AddFontFromMemoryTTF(PoppinsSemiBold, (int)sizeof(PoppinsSemiBold), 14.f, &cfg, io.Fonts->GetGlyphRangesDefault());
    font::regular_m = font::esp_font;
    font::brand_font = io.Fonts->AddFontFromMemoryTTF(PoppinsSemiBold, (int)sizeof(PoppinsSemiBold), 20.f, &cfg, io.Fonts->GetGlyphRangesDefault());
    font::bold_font = io.Fonts->AddFontFromMemoryTTF(PoppinsBold, (int)sizeof(PoppinsBold), 18.f, &cfg, io.Fonts->GetGlyphRangesDefault());
    if (!font::esp_font && io.Fonts->Fonts.Size > 0)
        font::esp_font = io.Fonts->Fonts[0];
    g_fontsReady = true;
}

void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(16.f, 12.f);
    s.FramePadding = ImVec2(10.f, 8.f);
    s.ItemSpacing = ImVec2(8.f, 6.f);
    s.ItemInnerSpacing = ImVec2(0.f, 0.f);
    s.FrameRounding = 8.f;
    s.WindowRounding = kRound;
    s.ChildRounding = kRound;
    s.GrabRounding = 8.f;
    s.FrameBorderSize = 0.f;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 0.f;
    s.PopupBorderSize = 0.f;
    s.WindowShadowSize = 0.f;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_Separator] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(6.f / 255.f, 6.f / 255.f, 6.f / 255.f, 1.f);
    s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(10.f / 255.f, 10.f / 255.f, 10.f / 255.f, 1.f);
    s.Colors[ImGuiCol_FrameBgActive] = ImVec4(12.f / 255.f, 12.f / 255.f, 12.f / 255.f, 1.f);
    s.Colors[ImGuiCol_Border] = ImVec4(0.14f, 0.14f, 0.14f, 1.f);
    s.Colors[ImGuiCol_Text] = utils::ImColorToImVec4(c::text::label::active);
    s.Colors[ImGuiCol_TextDisabled] = utils::ImColorToImVec4(c::text::label::default);
    s.Colors[ImGuiCol_TextSelectedBg] = ImVec4(1.f, 1.f, 1.f, 0.16f);
}

void field(const char* caption, const char* id, char* buf, int bufSize, float width, ImGuiInputTextFlags flags = 0) {
    if (font::s_inter_semibold)
        ImGui::PushFont(font::s_inter_semibold);
    ImGui::PushStyleColor(ImGuiCol_Text, utils::ImColorToImVec4(c::text::label::default));
    ImGui::TextUnformatted(caption);
    ImGui::PopStyleColor();
    if (font::s_inter_semibold)
        ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.f, 3.f));
    ImGui::PushID(id);
    ImGui::InputTextEx("", "", buf, bufSize, ImVec2(width, kFieldH), flags);
    ImGui::PopID();
}

void tryRestoreSession() {
    if (g_sessionChecked)
        return;
    g_sessionChecked = true;
    try {
        std::string token;
        AuthUser user;
        if (!g_auth.loadSession(token, user))
            return;
        g_token = token;
        g_user = user;
        g_view = View::LoggedIn;
    } catch (...) {
        g_auth.clearSession();
    }
}

void applyAuthResult(const AuthResult& r) {
    if (!r.ok) {
        setStatus(r.message.empty() ? "Wrong name or password" : r.message.c_str(), true);
        return;
    }
    g_token = r.token;
    g_user = r.user;
    g_auth.saveSession(g_token, g_user);
    g_view = View::LoggedIn;
    setStatus("", false);
    std::memset(g_password, 0, sizeof(g_password));
}

void pumpAuthResults() {
    AuthResult r;
    bool got = false;
    {
        std::lock_guard<std::mutex> lock(g_resultMu);
        if (g_gotResult) {
            r = g_pending;
            g_gotResult = false;
            got = true;
        }
    }
    if (got)
        applyAuthResult(r);
}

void submitAuth() {
    if (g_busy.load())
        return;
    const std::string name = g_name;
    const std::string password = g_password;
    if (name.size() < 3 || password.empty()) {
        setStatus("Wrong name or password", true);
        return;
    }

    g_busy.store(true);
    setStatus("Please wait...", false);
    const bool signup = g_mode == AuthMode::SignUp;
    std::thread([name, password, signup] {
        AuthResult r;
        try {
            AuthService svc;
            r = signup ? svc.signup(name, password) : svc.login(name, password);
        } catch (...) {
            r.ok = false;
            r.message = "Request failed";
        }
        {
            std::lock_guard<std::mutex> lock(g_resultMu);
            g_pending = r;
            g_gotResult = true;
        }
        g_busy.store(false);
    }).detach();
}

} // namespace

void initTheme() {
    initFonts();
    applyStyle();
}

void drawAuthScreen() {
    try {
        tryRestoreSession();
        c::enforce_mono_theme();

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(display, ImGuiCond_Always);

        ImGui::Begin("auth", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, wp + ws, col32(c::window_bg_color));

        if (font::esp_font)
            ImGui::PushFont(font::esp_font);

        if (font::brand_font)
            ImGui::PushFont(font::brand_font);
        ImGui::TextUnformatted(LOADER_BRAND);
        if (font::brand_font)
            ImGui::PopFont();

        ImGui::SameLine(ImGui::GetWindowWidth() - 34.f);
        if (ImGui::InvisibleButton("close", ImVec2(20.f, 20.f)))
            PostQuitMessage(0);
        {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            const ImU32 xc = ImGui::IsItemHovered() ? col32(c::text::label::active) : col32(c::text::label::default);
            dl->AddLine(ImVec2(mn.x + 5.f, mn.y + 5.f), ImVec2(mx.x - 5.f, mx.y - 5.f), xc, 1.5f);
            dl->AddLine(ImVec2(mx.x - 5.f, mn.y + 5.f), ImVec2(mn.x + 5.f, mx.y - 5.f), xc, 1.5f);
        }

        ImGui::Dummy(ImVec2(0.f, 8.f));
        const float inner_w = ImGui::GetContentRegionAvail().x;

        if (g_view == View::LoggedIn) {
            ImGui::TextUnformatted("Welcome");
            ImGui::PushStyleColor(ImGuiCol_Text, utils::ImColorToImVec4(c::text::label::default));
            ImGui::TextUnformatted(g_user.email.empty() ? "signed in" : g_user.email.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0.f, 6.f));
            if (custom::Button("Continue", ImVec2(inner_w, 36.f)))
                setStatus("", false);
            if (custom::Button("Log out", ImVec2(inner_w, 36.f))) {
                g_auth.clearSession();
                g_token.clear();
                g_user = {};
                g_view = View::Form;
                setStatus("", false);
            }
        } else {
            const float gap = 8.f;
            const float half = (inner_w - gap) * 0.5f;
            if (custom::Tab("Log in", g_mode == AuthMode::Login, ImVec2(half, 32.f)))
                g_mode = AuthMode::Login;
            ImGui::SameLine(0.f, gap);
            if (custom::Tab("Sign up", g_mode == AuthMode::SignUp, ImVec2(half, 32.f)))
                g_mode = AuthMode::SignUp;

            ImGui::Dummy(ImVec2(0.f, 10.f));
            field("Name", "name", g_name, IM_ARRAYSIZE(g_name), inner_w);
            ImGui::Dummy(ImVec2(0.f, 6.f));
            field("Password", "pass", g_password, IM_ARRAYSIZE(g_password), inner_w, ImGuiInputTextFlags_Password);

            ImGui::Dummy(ImVec2(0.f, 10.f));
            ImGui::BeginDisabled(g_busy.load());
            if (custom::Button(g_mode == AuthMode::Login ? "Sign in" : "Create account", ImVec2(inner_w, 36.f)))
                g_wantSubmit = true;
            ImGui::EndDisabled();
        }

        ImGui::Dummy(ImVec2(0.f, 8.f));
        ImGui::PushStyleColor(ImGuiCol_Text, g_error
            ? ImVec4(0.91f, 0.28f, 0.28f, 1.f)
            : utils::ImColorToImVec4(c::text::label::default));
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + inner_w);
        ImGui::TextUnformatted(g_status[0] ? g_status : " ");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        if (font::esp_font)
            ImGui::PopFont();
        ImGui::End();

        if (g_wantSubmit) {
            g_wantSubmit = false;
            submitAuth();
        }
        pumpAuthResults();
    } catch (...) {
        g_busy.store(false);
        g_wantSubmit = false;
        setStatus("Wrong name or password", true);
    }
}

} // namespace loader_ui
