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
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <shellapi.h>
#include <shlobj.h>

namespace loader_ui {
namespace {

enum class AuthMode { Login, SignUp };
enum class View { Form, LoggedIn };

AuthService g_auth;
View g_view = View::Form;
AuthMode g_mode = AuthMode::Login;
std::atomic<bool> g_busy{false};
bool g_wantSubmit = false;
bool g_wantRedeem = false;
bool g_redeemOpen = false;
std::atomic<bool> g_redeemBusy{false};
char g_key[96]{};
bool g_error = false;
char g_name[64]{};
char g_password[128]{};
char g_status[256]{};
std::string g_token;
AuthUser g_user;
bool g_fontsReady = false;
std::mutex g_resultMu;
bool g_gotResult = false;
AuthResult g_pending;
float g_expand = 0.f;
float g_spinAlpha = 0.f;

constexpr float kRound = 12.f;
constexpr float kFieldH = 34.f;
constexpr int kLoginW = 360;
constexpr int kLoginH = 356;
constexpr int kMainW = 648;
constexpr int kMainH = 392;

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

void drawCloseX(ImDrawList* dl) {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 36.f, ImGui::GetWindowPos().y + 10.f));
    if (ImGui::InvisibleButton("close", ImVec2(22.f, 22.f), ImGuiButtonFlags_PressedOnClick)) {
        if (g_app.hwnd)
            DestroyWindow(g_app.hwnd);
        ExitProcess(0);
    }
    const ImVec2 mn = ImGui::GetItemRectMin();
    const ImVec2 mx = ImGui::GetItemRectMax();
    const float cx = IM_ROUND((mn.x + mx.x) * 0.5f) + 0.5f;
    const float cy = IM_ROUND((mn.y + mx.y) * 0.5f) + 0.5f;
    const float r = 4.0f;
    const ImU32 xc = ImGui::IsItemHovered() ? IM_COL32(235, 235, 235, 255) : IM_COL32(140, 140, 140, 255);
    dl->AddLine(ImVec2(cx - r, cy - r), ImVec2(cx + r, cy + r), xc, 1.2f);
    dl->AddLine(ImVec2(cx + r, cy - r), ImVec2(cx - r, cy + r), xc, 1.2f);
}

void launchFiveM() {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.nShow = SW_SHOWNORMAL;
    sei.lpFile = L"fivem://";
    if (ShellExecuteExW(&sei))
        return;
    wchar_t localApp[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localApp))) {
        std::wstring exe = std::wstring(localApp) + L"\\FiveM\\FiveM.exe";
        sei.lpFile = exe.c_str();
        ShellExecuteExW(&sei);
    }
}

void drawFiveMProduct(ImDrawList* dl, const ImVec2& wp, const ImVec2& ws) {
    const float x = wp.x + 16.f;
    const float y = wp.y + 52.f;
    const float w = ws.x - 32.f;
    const float h = 64.f;
    const ImVec2 a(x, y);
    const ImVec2 b(x + w, y + h);
    dl->AddRectFilled(a, b, IM_COL32(16, 16, 18, 255), 10.f);
    dl->AddRect(a, b, IM_COL32(42, 42, 46, 255), 10.f, 0, 1.f);

    ImGui::SetCursorScreenPos(ImVec2(x + 18.f, y + (h - ImGui::GetFontSize()) * 0.5f));
    if (font::brand_font)
        ImGui::PushFont(font::brand_font);
    ImGui::TextUnformatted("FiveM");
    if (font::brand_font)
        ImGui::PopFont();

    const float pr = 16.f;
    const ImVec2 pc(b.x - 28.f, y + h * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(pc.x - pr, pc.y - pr));
    if (ImGui::InvisibleButton("play_fivem", ImVec2(pr * 2.f, pr * 2.f)))
        launchFiveM();
    const bool hov = ImGui::IsItemHovered();
    dl->AddCircleFilled(pc, pr, hov ? IM_COL32(48, 48, 52, 255) : IM_COL32(28, 28, 32, 255), 32);
    dl->AddCircle(pc, pr, IM_COL32(70, 70, 76, 255), 32, 1.f);
    dl->AddTriangleFilled(ImVec2(pc.x - 4.f, pc.y - 7.f), ImVec2(pc.x - 4.f, pc.y + 7.f), ImVec2(pc.x + 8.f, pc.y), IM_COL32(230, 230, 230, 255));
}

void drawSpinner(ImDrawList* dl, ImVec2 center, float radius, float alpha) {
    alpha = ImClamp(alpha, 0.f, 1.f);
    const float t = (float)ImGui::GetTime() * 2.55f;
    dl->PathArcTo(center, radius, 0.f, IM_PI * 2.f, 48);
    dl->PathStroke(IM_COL32(255, 255, 255, (int)(22.f * alpha)), ImDrawFlags_None, 2.0f);
    dl->PathArcTo(center, radius, t, t + 1.35f, 28);
    dl->PathStroke(IM_COL32(232, 232, 232, (int)(235.f * alpha)), ImDrawFlags_None, 2.35f);
}

void tickWindowExpand() {
    const float dt = ImGui::GetIO().DeltaTime;
    const float target = (g_view == View::LoggedIn) ? 1.f : 0.f;
    const float speed = 2.35f;
    if (g_expand < target)
        g_expand = ImMin(target, g_expand + dt * speed);
    else if (g_expand > target)
        g_expand = ImMax(target, g_expand - dt * speed);

    const float t = g_expand;
    const float e = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    const int w = (int)IM_ROUND(ImLerp((float)kLoginW, (float)kMainW, e));
    const int h = (int)IM_ROUND(ImLerp((float)kLoginH, (float)kMainH, e));
    g_app.setClientSizeCentered(w, h);
}

void tickSpinner(bool loading) {
    const float dt = ImGui::GetIO().DeltaTime;
    const float target = loading ? 1.f : 0.f;
    const float speed = loading ? 3.8f : 1.85f;
    if (g_spinAlpha < target)
        g_spinAlpha = ImMin(target, g_spinAlpha + dt * speed);
    else if (g_spinAlpha > target)
        g_spinAlpha = ImMax(target, g_spinAlpha - dt * speed);
}

void applyAuthResult(const AuthResult& r) {
    if (!r.ok) {
        setStatus(r.message.empty() ? "Wrong name or password" : r.message.c_str(), true);
        return;
    }
    if (g_view == View::LoggedIn) {
        if (!r.user.product.empty())
            g_user.product = r.user.product;
        g_user.lifetime = r.user.lifetime;
        g_user.expires = r.user.expires;
        g_auth.saveSession(g_token, g_user);
        setStatus(r.message.empty() ? "Key redeemed" : r.message.c_str(), false);
        std::memset(g_key, 0, sizeof(g_key));
        g_redeemOpen = false;
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
    setStatus("", false);
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

void submitRedeem() {
    if (g_redeemBusy.load() || g_token.empty())
        return;
    const std::string key = g_key;
    if (key.size() < 10) {
        setStatus("Invalid key", true);
        return;
    }
    g_redeemBusy.store(true);
    setStatus("", false);
    const std::string token = g_token;
    std::thread([key, token] {
        AuthResult r;
        try {
            AuthService svc;
            r = svc.redeem(token, key);
        } catch (...) {
            r.ok = false;
            r.message = "Redeem failed";
        }
        {
            std::lock_guard<std::mutex> lock(g_resultMu);
            g_pending = r;
            g_gotResult = true;
        }
        g_redeemBusy.store(false);
    }).detach();
}

} // namespace

void initTheme() {
    initFonts();
    applyStyle();
}

void drawAuthScreen() {
    try {
        c::enforce_mono_theme();
        tickWindowExpand();

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

        const bool busy = g_busy.load();
        const bool loading = busy || (g_view == View::LoggedIn && g_expand < 0.995f);
        tickSpinner(loading);
        const bool showForm = (g_view == View::Form && !busy);

        if (showForm) {
            if (font::esp_font)
                ImGui::PushFont(font::esp_font);

            ImGui::SetCursorPos(ImVec2(16.f, 12.f));
            if (font::brand_font)
                ImGui::PushFont(font::brand_font);
            ImGui::TextUnformatted(LOADER_BRAND);
            if (font::brand_font)
                ImGui::PopFont();

            ImGui::Dummy(ImVec2(0.f, 8.f));
            const float inner_w = ImGui::GetContentRegionAvail().x;

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
            if (custom::Button(g_mode == AuthMode::Login ? "Sign in" : "Create account", ImVec2(inner_w, 36.f)))
                g_wantSubmit = true;

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
        }
        if (g_spinAlpha > 0.01f) {
            drawSpinner(dl, wp + ws * 0.5f, ImLerp(12.f, 18.f, g_expand), g_spinAlpha);
        }

        if (g_view == View::LoggedIn && g_expand > 0.97f && g_spinAlpha < 0.25f) {
            ImGui::SetCursorScreenPos(ImVec2(wp.x + 14.f, wp.y + 10.f));
            if (custom::Button("Redeem key", ImVec2(112.f, 28.f)))
                g_redeemOpen = true;

            if (!g_user.product.empty())
                drawFiveMProduct(dl, wp, ws);

            if (g_redeemOpen) {
                const ImVec2 box(320.f, 168.f);
                const ImVec2 p0 = wp + (ws - box) * 0.5f;
                const ImVec2 p1 = p0 + box;
                dl->AddRectFilled(p0, p1, IM_COL32(14, 14, 14, 255), 10.f);
                dl->AddRect(p0, p1, IM_COL32(48, 48, 48, 255), 10.f, 0, 1.f);
                ImGui::SetCursorScreenPos(p0 + ImVec2(16.f, 14.f));
                ImGui::BeginGroup();
                ImGui::TextUnformatted("Redeem key");
                ImGui::Dummy(ImVec2(0.f, 8.f));
                ImGui::PushID("redeemkey");
                ImGui::InputTextEx("", "", g_key, IM_ARRAYSIZE(g_key), ImVec2(box.x - 32.f, 34.f), 0);
                ImGui::PopID();
                ImGui::Dummy(ImVec2(0.f, 10.f));
                ImGui::BeginDisabled(g_redeemBusy.load());
                if (custom::Button("Redeem", ImVec2(140.f, 32.f)))
                    g_wantRedeem = true;
                ImGui::EndDisabled();
                ImGui::SameLine(0.f, 8.f);
                if (custom::Button("Close", ImVec2(120.f, 32.f)))
                    g_redeemOpen = false;
                ImGui::Dummy(ImVec2(0.f, 6.f));
                ImGui::PushStyleColor(ImGuiCol_Text, g_error
                    ? ImVec4(0.91f, 0.28f, 0.28f, 1.f)
                    : utils::ImColorToImVec4(c::text::label::default));
                ImGui::TextUnformatted(g_status[0] ? g_status : " ");
                ImGui::PopStyleColor();
                ImGui::EndGroup();
            }
        }

        drawCloseX(dl);

        ImGui::End();

        if (g_wantSubmit) {
            g_wantSubmit = false;
            submitAuth();
        }
        if (g_wantRedeem) {
            g_wantRedeem = false;
            submitRedeem();
        }
        pumpAuthResults();
    } catch (...) {
        g_busy.store(false);
        g_wantSubmit = false;
        setStatus("Wrong name or password", true);
    }
}

} // namespace loader_ui
