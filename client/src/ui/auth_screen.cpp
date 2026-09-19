#include "auth_screen.hpp"
#include "config.hpp"
#include "auth_service.hpp"
#include "custom_widgets.hpp"
#include "imgui_settings.h"

#include "font.h"
#include "font_defines.h"

#include "imgui.h"

#include <cstring>

namespace loader_ui {
namespace {

enum class AuthMode { Login, SignUp };
enum class View { Form, LoggedIn };

AuthService g_auth;
View g_view = View::Form;
AuthMode g_mode = AuthMode::Login;
bool g_busy = false;
char g_email[256]{};
char g_password[256]{};
char g_confirm[256]{};
char g_status[512]{};
std::string g_token;
AuthUser g_user;
bool g_sessionChecked = false;

void setStatus(const char* msg) {
    strncpy_s(g_status, msg, _TRUNCATE);
}

void initFonts() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg{};

    static ImWchar icomoon_ranges[] = { 0x1, 0x10FFFD, 0 };
    static ImFontConfig icomoon_config;
    icomoon_config.OversampleH = icomoon_config.OversampleV = 1;
    icomoon_config.MergeMode = true;

    const float fs = c::ui::scale;
    icomoon_config.GlyphOffset.y = 2.f * fs;

    io.Fonts->AddFontFromMemoryTTF(PoppinsSemiBold, (int)sizeof(PoppinsSemiBold), 21.f * fs, &cfg, io.Fonts->GetGlyphRangesDefault());
    io.Fonts->AddFontFromMemoryCompressedBase85TTF(icomoon_compressed_data_base85, 20.f * fs, &icomoon_config, icomoon_ranges);

    font::esp_font = io.Fonts->AddFontFromMemoryTTF(PoppinsSemiBold, (int)sizeof(PoppinsSemiBold), 20.f * fs, &cfg, io.Fonts->GetGlyphRangesDefault());
    font::brand_font = io.Fonts->AddFontFromMemoryTTF(PoppinsSemiBold, (int)sizeof(PoppinsSemiBold), 27.f * fs, &cfg, io.Fonts->GetGlyphRangesDefault());
    font::regular_m = io.Fonts->AddFontFromMemoryTTF(PoppinsSemiBold, (int)sizeof(PoppinsSemiBold), 23.f * fs, &cfg, io.Fonts->GetGlyphRangesDefault());
    font::bold_font = io.Fonts->AddFontFromMemoryTTF(PoppinsBold, (int)sizeof(PoppinsBold), 25.f * fs, &cfg, io.Fonts->GetGlyphRangesDefault());
}

void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.FramePadding = ImVec2(c::ui::S(12.f), c::ui::S(7.f));
    s.ItemSpacing = ImVec2(c::layout::item_spacing, c::layout::item_spacing);
    s.FrameRounding = c::ui::S(8.f);
    s.WindowRounding = c::bg::rounding;
    s.WindowBorderSize = 0.f;
    s.PopupBorderSize = 0.f;
    s.WindowPadding = ImVec2(0, 0);
    s.ChildBorderSize = 0.f;
    s.Colors[ImGuiCol_Border] = ImVec4(0.f, 0.f, 0.f, 0.f);
    s.Colors[ImGuiCol_Separator] = ImVec4(1.f, 1.f, 1.f, 0.12f);
    s.Colors[ImGuiCol_FrameBg] = utils::ImColorToImVec4(c::child::background);
    s.Colors[ImGuiCol_FrameBgHovered] = c::elements::background_hovered;
    s.Colors[ImGuiCol_FrameBgActive] = c::elements::background_hovered;
    s.Colors[ImGuiCol_Text] = c::text::label::active;
    s.Colors[ImGuiCol_TextDisabled] = c::text::label::default;
    s.ScrollbarSize = c::ui::S(10.f);
    s.ScrollbarRounding = c::ui::S(6.f);
    s.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.06f, 0.06f, 0.06f, 1.f);
    s.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.35f, 0.35f, 0.35f, 1.f);
}

void drawChrome(const ImVec2& pos, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float rnd = c::bg::rounding;
    const float gap = c::layout::gap;
    const float head_h = c::layout::header_h;

    dl->AddRectFilled(pos, pos + size, ImGui::GetColorU32(utils::ImColorToImVec4(c::window_bg_color)), rnd);
    dl->AddRect(pos, pos + size, ImGui::GetColorU32(utils::ImColorToImVec4(c::border_color)), rnd, 0, 1.25f);

    ImFont* title_font = font::brand_font ? font::brand_font : font::esp_font;
    if (title_font) ImGui::PushFont(title_font);
    const char* brand = LOADER_BRAND;
    const ImVec2 brand_sz = ImGui::CalcTextSize(brand);
    dl->AddText(
        ImVec2(pos.x + gap + c::ui::S(16.f), pos.y + gap + (head_h - brand_sz.y) * 0.5f),
        ImGui::GetColorU32(utils::ImColorToImVec4(c::text::label::active)), brand);
    if (title_font) ImGui::PopFont();

    const float body_top = gap + head_h + gap;
    const ImVec2 content_min = pos + ImVec2(gap, body_top);
    const ImVec2 content_max = pos + ImVec2(size.x - gap, size.y - gap);
    dl->AddRectFilled(content_min, content_max, ImGui::GetColorU32(utils::ImColorToImVec4(c::content_bg)), c::layout::rounding);
}

void tryRestoreSession() {
    if (g_sessionChecked) return;
    g_sessionChecked = true;
    std::string token;
    AuthUser user;
    if (!g_auth.loadSession(token, user)) return;

    g_busy = true;
    setStatus("Checking session...");
    const AuthResult r = g_auth.me(token);
    g_busy = false;
    if (r.ok) {
        g_token = token;
        g_user = r.user;
        g_view = View::LoggedIn;
        setStatus("");
    } else {
        g_auth.clearSession();
    }
}

void styledInput(const char* label, char* buf, size_t bufSize, ImGuiInputTextFlags flags = 0) {
    ImGui::PushStyleColor(ImGuiCol_Text, utils::ImColorToImVec4(c::text::label::default));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputText((std::string("##") + label).c_str(), buf, bufSize, flags);
    ImGui::Dummy(ImVec2(0, c::ui::S(4.f)));
}

void submitAuth() {
    const std::string email = g_email;
    const std::string password = g_password;

    if (email.empty() || password.empty()) {
        setStatus("Email and password required.");
        return;
    }
    if (g_mode == AuthMode::SignUp) {
        if (std::strcmp(g_password, g_confirm) != 0) {
            setStatus("Passwords do not match.");
            return;
        }
        if (password.size() < 8) {
            setStatus("Password must be at least 8 characters.");
            return;
        }
    }

    g_busy = true;
    setStatus(g_mode == AuthMode::Login ? "Signing in..." : "Creating account...");

    const AuthResult r = (g_mode == AuthMode::Login)
        ? g_auth.login(email, password)
        : g_auth.signup(email, password);

    g_busy = false;
    if (!r.ok) {
        setStatus(r.message.c_str());
        return;
    }

    g_token = r.token;
    g_user = r.user;
    g_auth.saveSession(g_token, g_user);
    g_view = View::LoggedIn;
    setStatus("");
    std::memset(g_password, 0, sizeof(g_password));
    std::memset(g_confirm, 0, sizeof(g_confirm));
}

} // namespace

void initTheme() {
    initFonts();
    applyStyle();
}

void drawAuthScreen() {
    tryRestoreSession();

    const ImVec2 winSize(c::ui::S(440.f), c::ui::S(520.f));
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 winPos((display.x - winSize.x) * 0.5f, (display.y - winSize.y) * 0.5f);

    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);

    ImGui::Begin("##loader_auth", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

    drawChrome(winPos, winSize);

    const float body_top = c::layout::body_top();
    ImGui::SetCursorPos(ImVec2(c::layout::gap + c::layout::panel_pad_x, body_top + c::layout::panel_pad_y));

    const float inner_w = winSize.x - c::layout::gap * 2.f - c::layout::panel_pad_x * 2.f;
    ImGui::PushItemWidth(inner_w);

    if (g_view == View::LoggedIn) {
        if (font::regular_m) ImGui::PushFont(font::regular_m);
        ImGui::TextColored(c::text::label::active, "Welcome");
        ImGui::TextColored(c::text::label::default, "%s", g_user.email.c_str());
        if (font::regular_m) ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, c::ui::S(16.f)));
        if (custom::Button("Continue", ImVec2(inner_w, c::layout::btn_h))) {
            // v1: stay logged in; button clears status only
            setStatus("Ready.");
        }
        ImGui::Dummy(ImVec2(0, c::ui::S(8.f)));
        if (custom::Button("Log out", ImVec2(inner_w, c::layout::btn_h))) {
            g_auth.clearSession();
            g_token.clear();
            g_user = {};
            g_view = View::Form;
            setStatus("");
        }
    } else {
        const float half = (inner_w - c::layout::item_spacing) * 0.5f;
        if (custom::Button("Log in", ImVec2(half, c::layout::btn_h)))
            g_mode = AuthMode::Login;
        ImGui::SameLine();
        if (custom::Button("Sign up", ImVec2(half, c::layout::btn_h)))
            g_mode = AuthMode::SignUp;

        ImGui::Dummy(ImVec2(0, c::ui::S(12.f)));
        styledInput("Email", g_email, sizeof(g_email));
        styledInput("Password", g_password, sizeof(g_password), ImGuiInputTextFlags_Password);
        if (g_mode == AuthMode::SignUp)
            styledInput("Confirm password", g_confirm, sizeof(g_confirm), ImGuiInputTextFlags_Password);

        ImGui::Dummy(ImVec2(0, c::ui::S(8.f)));
        const char* action = g_mode == AuthMode::Login ? "Sign in" : "Create account";
        ImGui::BeginDisabled(g_busy);
        if (custom::Button(action, ImVec2(inner_w, c::layout::btn_h)))
            submitAuth();
        ImGui::EndDisabled();

        if (g_status[0]) {
            ImGui::Dummy(ImVec2(0, c::ui::S(8.f)));
            ImGui::TextColored(c::text::label::default, "%s", g_status);
        }
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

} // namespace loader_ui
