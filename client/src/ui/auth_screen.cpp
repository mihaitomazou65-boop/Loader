#define IMGUI_DEFINE_MATH_OPERATORS
#include "auth_screen.hpp"
#include "app_window.hpp"
#include "config.hpp"
#include "obfuscate.hpp"
#include "auth_service.hpp"
#include "http_client.hpp"
#include "custom_widgets.hpp"
#include "imgui_settings.h"
#include "font.h"
#include "font_defines.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "resource.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <map>
#include <vector>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

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
char g_redeemStatus[256]{};
bool g_redeemError = false;
float g_redeemStatusTimer = 0.f;
char g_key[96]{};
char g_fivemPath[520]{};
bool g_fivemPathLoaded = false;
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
std::atomic<bool> g_playBusy{false};
char g_playMsg[128]{};
std::string g_playProduct;
ID3D11ShaderResourceView* g_fivemBanner = nullptr;
int g_fivemBannerW = 0;
int g_fivemBannerH = 0;
struct LiveThumb {
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0;
    int h = 0;
    std::string ver;
    float fx = 0.5f;
    float fy = 0.5f;
};
std::map<std::string, LiveThumb> g_liveThumbs;
std::atomic<bool> g_syncBusy{false};
float g_syncTimer = 8.f;
std::mutex g_thumbBytesMu;
struct PendingThumb {
    std::string product;
    std::string bytes;
    std::string ver;
    float fx = 0.5f;
    float fy = 0.5f;
};
std::vector<PendingThumb> g_pendingThumbs;
float g_prodScroll = 0.f;

constexpr float kRound = 12.f;
constexpr float kFieldH = 34.f;
constexpr int kLoginW = 360;
constexpr int kLoginH = 356;
constexpr int kMainW = 648;
constexpr int kMainH = 448;

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

void setRedeemStatus(const char* msg, bool error, float successAutoClearSec = 0.f) {
    g_redeemError = error;
    g_redeemStatusTimer = (!error && successAutoClearSec > 0.f) ? successAutoClearSec : 0.f;
    if (!msg || !msg[0]) {
        g_redeemStatus[0] = 0;
        g_redeemError = false;
        g_redeemStatusTimer = 0.f;
        return;
    }
    strncpy_s(g_redeemStatus, msg, _TRUNCATE);
}

void openRedeemDialog() {
    g_redeemOpen = true;
    setRedeemStatus("", false);
    g_redeemBusy.store(false);
}

void tickRedeemStatusTimer() {
    if (g_redeemStatusTimer <= 0.f)
        return;
    g_redeemStatusTimer -= ImGui::GetIO().DeltaTime;
    if (g_redeemStatusTimer <= 0.f && !g_redeemError)
        setRedeemStatus("", false);
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
    s.Colors[ImGuiCol_PopupBg] = ImVec4(18.f / 255.f, 18.f / 255.f, 20.f / 255.f, 1.f);
    s.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.f, 0.f, 0.f, 0.28f);
}

void applyPaste(char* buf, int bufSize, ImGuiID textId = 0) {
    if (!buf || bufSize < 2)
        return;
    const std::string clip = OsClipboardUtf8();
    if (clip.empty())
        return;
    std::string out = clip;
    if (static_cast<int>(out.size()) >= bufSize)
        out.resize(static_cast<size_t>(bufSize - 1));
    std::memset(buf, 0, static_cast<size_t>(bufSize));
    std::memcpy(buf, out.c_str(), out.size());

    const ImGuiID id = textId ? textId : ImGui::GetItemID();
    if (ImGuiInputTextState* st = ImGui::GetInputTextState(id)) {
        st->TextW.resize(bufSize + 1);
        st->CurLenW = ImTextStrFromUtf8(st->TextW.Data, bufSize, buf, nullptr, nullptr);
        st->CurLenA = static_cast<int>(std::strlen(buf));
        st->TextAIsValid = false;
        st->Stb.cursor = st->CurLenW;
        st->Stb.select_start = st->Stb.select_end = st->CurLenW;
        st->CursorFollow = true;
    }
}

bool textInput(const char* id, char* buf, int bufSize, float width, float height, ImGuiInputTextFlags flags = 0) {
    ImGui::PushID(id);
    bool changed = ImGui::InputTextEx("##in", "", buf, bufSize, ImVec2(width, height), flags);
    const ImGuiID textId = ImGui::GetItemID();
    if (g_pasteQueued && (ImGui::IsItemActive() || ImGui::IsItemHovered())) {
        applyPaste(buf, bufSize, textId);
        g_pasteQueued = false;
        changed = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        applyPaste(buf, bufSize, textId);
        changed = true;
    }
    ImGui::PopID();
    return changed;
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
    textInput(id, buf, bufSize, width, kFieldH, flags);
}

void drawCloseX(ImDrawList* dl) {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 36.f, ImGui::GetWindowPos().y + 10.f));
    if (ImGui::InvisibleButton(OBF("close"), ImVec2(22.f, 22.f), ImGuiButtonFlags_PressedOnClick)) {
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

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0')
        out.pop_back();
    return out;
}

void loadFiveMPath() {
    if (g_fivemPathLoaded)
        return;
    g_fivemPathLoaded = true;
    wchar_t dir[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, dir)))
        return;
    const std::wstring path = std::wstring(dir) + OBFW(L"\\Loader\\fivem_path.txt");
    std::ifstream f(path);
    if (!f)
        return;
    std::string line;
    std::getline(f, line);
    if (line.size() >= sizeof(g_fivemPath))
        line.resize(sizeof(g_fivemPath) - 1);
    std::memset(g_fivemPath, 0, sizeof(g_fivemPath));
    std::memcpy(g_fivemPath, line.c_str(), line.size());
}

void saveFiveMPath() {
    wchar_t dir[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, dir)))
        return;
    const std::wstring folder = std::wstring(dir) + OBFW(L"\\Loader");
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring path = folder + OBFW(L"\\fivem_path.txt");
    std::ofstream f(path, std::ios::trunc);
    f << g_fivemPath;
}

std::wstring cleanedFiveMExe() {
    std::string p = g_fivemPath;
    while (!p.empty() && (p.front() == '"' || p.front() == ' '))
        p.erase(p.begin());
    while (!p.empty() && (p.back() == '"' || p.back() == ' ' || p.back() == '\r' || p.back() == '\n'))
        p.pop_back();
    if (p.rfind("file:///", 0) == 0)
        p = p.substr(8);
    else if (p.rfind("file://", 0) == 0)
        p = p.substr(7);
    for (char& c : p) {
        if (c == '/')
            c = '\\';
    }
    return utf8ToWide(p);
}

std::wstring productDirName(const std::string& product) {
    std::string safe;
    for (char c : product) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_')
            safe += c;
        else if (c == '.')
            safe += '_';
    }
    while (!safe.empty() && safe.front() == ' ')
        safe.erase(safe.begin());
    while (!safe.empty() && safe.back() == ' ')
        safe.pop_back();
    if (safe.empty())
        safe = "Product";
    return utf8ToWide(safe);
}

void resetLiveThumbs() {
    for (auto& kv : g_liveThumbs) {
        if (kv.second.srv)
            kv.second.srv->Release();
    }
    g_liveThumbs.clear();
    {
        std::lock_guard<std::mutex> lock(g_thumbBytesMu);
        g_pendingThumbs.clear();
    }
}

std::wstring urlEncodeQueryUtf8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size() * 3);
    auto hex = [](unsigned v) -> wchar_t { return (v < 10) ? (L'0' + v) : (L'A' + (v - 10)); };
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
            out.push_back((wchar_t)c);
        else {
            out.push_back(L'%');
            out.push_back(hex(c >> 4));
            out.push_back(hex(c & 0xf));
        }
    }
    return out;
}

const ProductEntitlement* findEntitlement(const std::string& product) {
    for (const ProductEntitlement& p : g_user.products) {
        if (p.product == product)
            return &p;
    }
    return nullptr;
}

void launchProduct(const std::string& product);
bool hasActiveProduct();
void tickLiveProduct();
void pruneExpiredProducts();

void launchProduct(const std::string& product) {
    if (g_playBusy.load())
        return;
    const ProductEntitlement* ent = findEntitlement(product);
    if (!ent)
        return;
    const std::string token = g_token;
    const std::string ver = ent->fileVersion.empty() ? OBF("live") : ent->fileVersion;
    std::string name = ent->fileName.empty() ? OBF("product.exe") : ent->fileName;
    g_playProduct = product;
    for (char& c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            c = '_';
    }
    g_playBusy.store(true);
    std::snprintf(g_playMsg, sizeof(g_playMsg), "%s", OBF("Loading..."));
    std::thread([token, product, ver, name] {
        std::wstring dest;
        auto finishLaunch = [&](const std::wstring& exe) {
            SHELLEXECUTEINFOW sei{};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_FLAG_NO_UI;
            sei.lpVerb = OBFW(L"open");
            sei.nShow = SW_SHOWNORMAL;
            sei.lpFile = exe.c_str();
            if (!ShellExecuteExW(&sei))
                return false;
            if (HWND hwnd = g_app.hwnd)
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return true;
        };
        if (!product.empty() && !token.empty()) {
            wchar_t app[MAX_PATH]{};
            SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, app);
            const std::wstring prodDir = productDirName(product);
            const std::wstring dir =
                std::wstring(app) + OBFW(L"\\Loader\\products\\") + prodDir + L"\\" + utf8ToWide(ver);
            CreateDirectoryW((std::wstring(app) + OBFW(L"\\Loader")).c_str(), nullptr);
            CreateDirectoryW((std::wstring(app) + OBFW(L"\\Loader\\products")).c_str(), nullptr);
            CreateDirectoryW((std::wstring(app) + OBFW(L"\\Loader\\products\\") + prodDir).c_str(), nullptr);
            CreateDirectoryW(dir.c_str(), nullptr);
            dest = dir + L"\\" + utf8ToWide(name);
            {
                std::snprintf(g_playMsg, sizeof(g_playMsg), "%s", OBF("Downloading..."));
                const std::wstring path =
                    OBFW(L"/auth/product-file?product=") + urlEncodeQueryUtf8(product);
                const HttpResponse resp = HttpClient::request(OBFW(L"GET"), path, {}, token);
                if (resp.status < 200 || resp.status >= 300 || resp.body.empty()) {
                    std::snprintf(g_playMsg, sizeof(g_playMsg), "%s", OBF("No product file"));
                    g_playBusy.store(false);
                    return;
                }
                std::ofstream out(dest, std::ios::binary | std::ios::trunc);
                out.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
                out.close();
                if (!out) {
                    std::snprintf(g_playMsg, sizeof(g_playMsg), "%s", OBF("Save failed"));
                    g_playBusy.store(false);
                    return;
                }
            }
            if (finishLaunch(dest)) {
                g_playMsg[0] = 0;
                return;
            }
            std::snprintf(g_playMsg, sizeof(g_playMsg), "%s", OBF("Launch failed"));
            g_playBusy.store(false);
            return;
        }
        const std::wstring custom = cleanedFiveMExe();
        if (!custom.empty()) {
            if (finishLaunch(custom)) {
                g_playMsg[0] = 0;
                return;
            }
            std::snprintf(g_playMsg, sizeof(g_playMsg), "%s", OBF("Launch failed"));
            g_playBusy.store(false);
            return;
        }
        std::snprintf(g_playMsg, sizeof(g_playMsg), "%s", OBF("No product file"));
        g_playBusy.store(false);
    }).detach();
}

time_t parseExpiresUtc(const std::string& iso) {
    int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss) < 6)
        return 0;
    std::tm t{};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = ss;
    return _mkgmtime(&t);
}

void formatRemainFor(const ProductEntitlement& ent, char* out, size_t cap) {
    if (!out || cap < 8)
        return;
    if (ent.lifetime) {
        std::snprintf(out, cap, "%s", OBF("Lifetime"));
        return;
    }
    const time_t exp = parseExpiresUtc(ent.expires);
    const time_t now = time(nullptr);
    if (exp <= 0) {
        std::snprintf(out, cap, "%s", OBF("No time left"));
        return;
    }
    long long left = (long long)exp - (long long)now;
    if (left <= 0) {
        std::snprintf(out, cap, "%s", OBF("Expired"));
        return;
    }
    const long long days = left / 86400;
    const long long hours = (left % 86400) / 3600;
    const long long mins = (left % 3600) / 60;
    if (days >= 60) {
        const long long months = days / 30;
        const long long rd = days % 30;
        if (rd)
            std::snprintf(out, cap, "%lld months %lld days left", months, rd);
        else
            std::snprintf(out, cap, "%lld months left", months);
    } else if (days >= 1) {
        if (hours)
            std::snprintf(out, cap, "%lld day%s %lld hour%s left", days, days == 1 ? "" : "s", hours, hours == 1 ? "" : "s");
        else
            std::snprintf(out, cap, "%lld day%s left", days, days == 1 ? "" : "s");
    } else if (hours >= 1) {
        if (mins)
            std::snprintf(out, cap, "%lld hour%s %lld min left", hours, hours == 1 ? "" : "s", mins);
        else
            std::snprintf(out, cap, "%lld hour%s left", hours, hours == 1 ? "" : "s");
    } else if (mins >= 1) {
        std::snprintf(out, cap, "%lld min left", mins);
    } else {
        std::snprintf(out, cap, "%s", OBF("Expires soon"));
    }
}

bool entitlementActive(const ProductEntitlement& ent) {
    if (ent.product.empty())
        return false;
    if (ent.lifetime)
        return true;
    if (ent.expires.empty())
        return true;
    const time_t exp = parseExpiresUtc(ent.expires);
    if (exp <= 0)
        return true;
    return exp > time(nullptr);
}

void pruneExpiredProducts() {
    bool changed = false;
    for (auto it = g_user.products.begin(); it != g_user.products.end();) {
        if (entitlementActive(*it))
            ++it;
        else {
            it = g_user.products.erase(it);
            changed = true;
        }
    }
    if (changed) {
        if (!g_user.products.empty()) {
            const ProductEntitlement& p = g_user.products.front();
            g_user.product = p.product;
            g_user.lifetime = p.lifetime;
            g_user.expires = p.expires;
            g_user.fileName = p.fileName;
            g_user.fileVersion = p.fileVersion;
            g_user.thumbVersion = p.thumbVersion;
            g_user.thumbFx = p.thumbFx;
            g_user.thumbFy = p.thumbFy;
        } else {
            g_user.product.clear();
            g_user.fileName.clear();
            g_user.fileVersion.clear();
            g_user.thumbVersion.clear();
        }
        g_auth.saveSession(g_token, g_user);
    }
}

bool hasActiveProduct() {
    for (const ProductEntitlement& p : g_user.products) {
        if (!p.product.empty())
            return true;
    }
    return false;
}

bool createTextureFromBytes(const void* bytes, size_t nbytes, ID3D11ShaderResourceView** outSrv, int* outW, int* outH) {
    if (!bytes || !nbytes || !g_app.device || !outSrv)
        return false;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID3D11Texture2D* tex = nullptr;
    auto fail = [&]() {
        if (tex) tex->Release();
        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        if (stream) stream->Release();
        if (factory) factory->Release();
        return false;
    };
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;
    if (FAILED(factory->CreateStream(&stream))) return fail();
    if (FAILED(stream->InitializeFromMemory((BYTE*)bytes, (DWORD)nbytes))) return fail();
    if (FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) return fail();
    if (FAILED(decoder->GetFrame(0, &frame))) return fail();
    if (FAILED(factory->CreateFormatConverter(&converter))) return fail();
    if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return fail();
    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    if (!w || !h) return fail();
    std::vector<BYTE> pixels((size_t)w * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data()))) return fail();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA upload{};
    upload.pSysMem = pixels.data();
    upload.SysMemPitch = w * 4;
    if (FAILED(g_app.device->CreateTexture2D(&desc, &upload, &tex))) return fail();
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(g_app.device->CreateShaderResourceView(tex, nullptr, &srv))) return fail();
    tex->Release();
    converter->Release();
    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();
    *outSrv = srv;
    if (outW) *outW = (int)w;
    if (outH) *outH = (int)h;
    return true;
}

void ensureFiveMBanner() {
    if (g_fivemBanner || !g_app.device)
        return;
    const HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_FIVEM_BANNER), RT_RCDATA);
    if (!res)
        return;
    const HGLOBAL mem = LoadResource(nullptr, res);
    if (!mem)
        return;
    const DWORD nbytes = SizeofResource(nullptr, res);
    void* bytes = LockResource(mem);
    if (!bytes || !nbytes)
        return;
    createTextureFromBytes(bytes, nbytes, &g_fivemBanner, &g_fivemBannerW, &g_fivemBannerH);
}

void applyPendingThumbs() {
    std::vector<PendingThumb> batch;
    {
        std::lock_guard<std::mutex> lock(g_thumbBytesMu);
        if (g_pendingThumbs.empty())
            return;
        batch.swap(g_pendingThumbs);
    }
    for (PendingThumb& pt : batch) {
        if (pt.bytes.empty())
            continue;
        ID3D11ShaderResourceView* srv = nullptr;
        int w = 0, h = 0;
        if (!createTextureFromBytes(pt.bytes.data(), pt.bytes.size(), &srv, &w, &h))
            continue;
        LiveThumb& slot = g_liveThumbs[pt.product];
        if (slot.srv)
            slot.srv->Release();
        slot.srv = srv;
        slot.w = w;
        slot.h = h;
        slot.ver = pt.ver;
        slot.fx = pt.fx;
        slot.fy = pt.fy;
    }
}

void tickLiveProduct() {
    applyPendingThumbs();
    if (g_view != View::LoggedIn || g_token.empty() || g_syncBusy.load())
        return;
    g_syncTimer += ImGui::GetIO().DeltaTime;
    const bool needPoll = g_syncTimer >= 3.f;
    bool needImage = false;
    if (hasActiveProduct()) {
        for (const ProductEntitlement& p : g_user.products) {
            if (p.thumbVersion.empty())
                continue;
            const auto it = g_liveThumbs.find(p.product);
            if (it == g_liveThumbs.end() || it->second.ver != p.thumbVersion) {
                needImage = true;
                break;
            }
        }
    }
    if (!needPoll && !needImage)
        return;
    g_syncTimer = 0.f;
    g_syncBusy.store(true);
    const std::string token = g_token;
    std::map<std::string, std::string> loadedVers;
    for (const auto& kv : g_liveThumbs)
        loadedVers[kv.first] = kv.second.ver;
    std::thread([token, loadedVers] {
        AuthService svc;
        AuthResult r;
        try {
            r = svc.me(token);
        } catch (...) {
            r.ok = false;
        }
        r.liveSync = true;
        {
            std::lock_guard<std::mutex> lock(g_resultMu);
            g_pending = r;
            g_gotResult = true;
        }
        if (r.ok) {
            for (const ProductEntitlement& p : r.user.products) {
                if (p.thumbVersion.empty())
                    continue;
                const auto prev = loadedVers.find(p.product);
                if (prev != loadedVers.end() && prev->second == p.thumbVersion)
                    continue;
                try {
                    const std::wstring path =
                        OBFW(L"/auth/product-thumb?product=") + urlEncodeQueryUtf8(p.product);
                    const HttpResponse img = HttpClient::request(OBFW(L"GET"), path, {}, token);
                    if (img.status >= 200 && img.status < 300 && !img.body.empty()) {
                        PendingThumb pt;
                        pt.product = p.product;
                        pt.bytes = img.body;
                        pt.ver = p.thumbVersion;
                        pt.fx = p.thumbFx;
                        pt.fy = p.thumbFy;
                        std::lock_guard<std::mutex> lock(g_thumbBytesMu);
                        g_pendingThumbs.push_back(std::move(pt));
                    }
                } catch (...) {
                }
            }
        }
        g_syncBusy.store(false);
    }).detach();
}

void coverUv(float boxW, float boxH, float imgW, float imgH, float fx, float fy, ImVec2& uv0, ImVec2& uv1) {
    uv0 = ImVec2(0.f, 0.f);
    uv1 = ImVec2(1.f, 1.f);
    if (boxW < 1.f || boxH < 1.f || imgW < 1.f || imgH < 1.f)
        return;
    fx = ImClamp(fx, 0.f, 1.f);
    fy = ImClamp(fy, 0.f, 1.f);
    const float boxA = boxW / boxH;
    const float imgA = imgW / imgH;
    if (imgA > boxA) {
        const float vis = boxA / imgA;
        uv0.x = (1.f - vis) * fx;
        uv1.x = uv0.x + vis;
    } else {
        const float vis = imgA / boxA;
        uv0.y = (1.f - vis) * fy;
        uv1.y = uv0.y + vis;
    }
    const float du = 0.75f / imgW;
    const float dv = 0.75f / imgH;
    uv0.x += du;
    uv0.y += dv;
    uv1.x -= du;
    uv1.y -= dv;
}

void drawProductCard(ImDrawList* dl, const ProductEntitlement& ent, const ImVec2& p0, float cardW, int id) {
    if (ent.product == OBF("FiveM"))
        ensureFiveMBanner();
    const float cardH = 108.f;
    const float rnd = 12.f;
    const ImDrawFlags roundAll = ImDrawFlags_RoundCornersAll;

    const float x0 = floorf(p0.x);
    const float y0 = floorf(p0.y);
    const float x1 = x0 + floorf(cardW);
    const float y1 = y0 + floorf(cardH);
    const ImVec2 a0(x0, y0);
    const ImVec2 a1(x1, y1);
    const float iw = ImMax(1.f, x1 - x0);
    const float ih = ImMax(1.f, y1 - y0);

    dl->AddRectFilled(a0, a1, IM_COL32(10, 10, 12, 255), rnd, roundAll);

    LiveThumb* live = nullptr;
    const auto it = g_liveThumbs.find(ent.product);
    if (it != g_liveThumbs.end())
        live = &it->second;
    ID3D11ShaderResourceView* fallback = (ent.product == OBF("FiveM")) ? g_fivemBanner : nullptr;
    ID3D11ShaderResourceView* tex = nullptr;
    if (live && live->srv)
        tex = live->srv;
    else
        tex = fallback;
    if (tex) {
        const float tw = (live && live->srv) ? (float)live->w : (float)g_fivemBannerW;
        const float th = (live && live->srv) ? (float)live->h : (float)g_fivemBannerH;
        const float fx = live ? live->fx : ent.thumbFx;
        const float fy = live ? live->fy : ent.thumbFy;
        ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
        coverUv(iw, ih, tw, th, fx, fy, uv0, uv1);
        dl->AddImageRounded((ImTextureID)tex, a0, a1, uv0, uv1, IM_COL32(255, 255, 255, 255), rnd, roundAll);
        dl->AddRectFilled(a0, a1, IM_COL32(0, 0, 0, 118), rnd, roundAll);
    }

    dl->AddRect(a0, a1, IM_COL32(255, 255, 255, 42), rnd, roundAll, 1.f);

    const char* prodLabel = ent.product.empty() ? OBF("Product") : ent.product.c_str();
    ImFont* titleFont = font::brand_font ? font::brand_font : ImGui::GetFont();
    const float titleSize = titleFont ? titleFont->FontSize : 18.f;
    dl->AddText(titleFont, titleSize, ImVec2(a0.x + 19.f, a0.y + 17.f), IM_COL32(0, 0, 0, 184), prodLabel);
    dl->AddText(titleFont, titleSize, ImVec2(a0.x + 18.f, a0.y + 16.f), IM_COL32(255, 255, 255, 255), prodLabel);

    char remain[64]{};
    formatRemainFor(ent, remain, sizeof(remain));
    const bool showPlayMsg = g_playMsg[0] && g_playProduct == ent.product;
    const char* sub = showPlayMsg ? g_playMsg : remain;
    ImFont* bodyFont = font::s_inter_semibold ? font::s_inter_semibold : ImGui::GetFont();
    const float bodySize = bodyFont ? bodyFont->FontSize : 14.f;
    dl->AddText(bodyFont, bodySize, ImVec2(a0.x + 18.f, a0.y + 44.f), IM_COL32(230, 230, 237, 235), sub);

    const float box = 46.f;
    const float boxR = 10.f;
    const ImVec2 b0(floorf(a1.x - 16.f - box), floorf((a0.y + a1.y) * 0.5f - box * 0.5f));
    ImGui::PushID(ent.product.c_str());
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(b0);
    ImGui::BeginDisabled(g_playBusy.load() || g_redeemOpen);
    if (ImGui::InvisibleButton("play", ImVec2(box, box)))
        launchProduct(ent.product);
    ImGui::EndDisabled();
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    ImGui::PopID();
    dl->AddRectFilled(b0, ImVec2(b0.x + box, b0.y + box), hov ? IM_COL32(58, 60, 66, 255) : IM_COL32(42, 44, 50, 255), boxR, roundAll);
    dl->AddRect(b0, ImVec2(b0.x + box, b0.y + box), hov ? IM_COL32(210, 212, 218, 70) : IM_COL32(255, 255, 255, 38), boxR, roundAll, 1.f);

    const ImVec2 pc(b0.x + box * 0.5f + 1.35f, b0.y + box * 0.5f);
    const float triW = 11.2f;
    const float triH = 12.8f;
    dl->PathLineTo(ImVec2(pc.x - triW * 0.42f, pc.y - triH * 0.5f));
    dl->PathLineTo(ImVec2(pc.x + triW * 0.62f, pc.y));
    dl->PathLineTo(ImVec2(pc.x - triW * 0.42f, pc.y + triH * 0.5f));
    dl->PathFillConvex(hov ? IM_COL32(248, 248, 250, 255) : IM_COL32(232, 233, 238, 255));
}

void drawProductList(ImDrawList* dl, const ImVec2& wp, const ImVec2& ws) {
    if (!dl)
        return;
    const float cardH = 108.f;
    const float gap = 12.f;
    const float stride = cardH + gap;
    const float x = floorf(wp.x + 16.f);
    const float top = floorf(wp.y + 52.f);
    const float w = floorf(ws.x - 32.f);
    const float viewH = floorf(ws.y - 64.f);

    std::vector<ProductEntitlement> rows;
    rows.reserve(g_user.products.size());
    for (const ProductEntitlement& p : g_user.products) {
        if (!p.product.empty())
            rows.push_back(p);
    }
    if (rows.empty())
        return;

    const float contentH = (float)rows.size() * cardH + (float)ImMax(0, (int)rows.size() - 1) * gap;
    const float maxScroll = ImMax(0.f, contentH - viewH);
    if (ImGui::IsMouseHoveringRect(ImVec2(x, top), ImVec2(x + w, top + viewH), false) && !g_redeemOpen)
        g_prodScroll -= ImGui::GetIO().MouseWheel * 48.f;
    g_prodScroll = ImClamp(g_prodScroll, 0.f, maxScroll);

    if (ImGuiWindow* wnd = ImGui::GetCurrentWindow()) {
        wnd->SkipItems = false;
        wnd->Hidden = false;
    }
    dl->PushClipRect(ImVec2(x, top), ImVec2(x + w, top + viewH), false);
    for (int row = 0; row < (int)rows.size(); ++row) {
        const float y = top + (float)row * stride - g_prodScroll;
        drawProductCard(dl, rows[(size_t)row], ImVec2(x, y), w, 9000 + row);
    }
    dl->PopClipRect();
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

void upsertProduct(std::vector<ProductEntitlement>& list, const ProductEntitlement& incoming) {
    if (incoming.product.empty())
        return;
    for (ProductEntitlement& p : list) {
        if (p.product == incoming.product) {
            p = incoming;
            return;
        }
    }
    list.push_back(incoming);
}

ProductEntitlement entitlementFromUser(const AuthUser& u) {
    ProductEntitlement p;
    p.product = u.product;
    p.lifetime = u.lifetime;
    p.expires = u.expires;
    p.fileName = u.fileName;
    p.fileVersion = u.fileVersion;
    p.thumbVersion = u.thumbVersion;
    p.thumbFx = u.thumbFx;
    p.thumbFy = u.thumbFy;
    return p;
}

void mergeUserFromServer(const AuthUser& src) {
    if (!src.id.empty())
        g_user.id = src.id;
    if (!src.email.empty())
        g_user.email = src.email;

    if (src.productsFromServer) {
        g_user.products = src.products;
        g_user.productsFromServer = true;
    } else if (!src.products.empty()) {
        for (const ProductEntitlement& incoming : src.products)
            upsertProduct(g_user.products, incoming);
    } else if (!src.product.empty()) {
        upsertProduct(g_user.products, entitlementFromUser(src));
    }

    pruneExpiredProducts();
    if (!g_user.products.empty()) {
        const ProductEntitlement& p = g_user.products.front();
        g_user.product = p.product;
        g_user.lifetime = p.lifetime;
        g_user.expires = p.expires;
        g_user.fileName = p.fileName;
        g_user.fileVersion = p.fileVersion;
        g_user.thumbVersion = p.thumbVersion;
        g_user.thumbFx = p.thumbFx;
        g_user.thumbFy = p.thumbFy;
    } else {
        g_user.product.clear();
        g_user.fileName.clear();
        g_user.fileVersion.clear();
        g_user.thumbVersion.clear();
        g_user.lifetime = false;
        g_user.expires.clear();
    }
}

void applyAuthResult(const AuthResult& r) {
    if (!r.ok) {
        if (g_view == View::LoggedIn && g_redeemOpen)
            setRedeemStatus(r.message.empty() ? OBF("Can't redeem this key") : r.message.c_str(), true);
        else
            setStatus(r.message.empty() ? OBF("Wrong name or password") : r.message.c_str(), true);
        return;
    }
    if (r.liveSync) {
        mergeUserFromServer(r.user);
        g_auth.saveSession(g_token, g_user);
        return;
    }
    if (g_view == View::LoggedIn) {
        mergeUserFromServer(r.user);
        g_auth.saveSession(g_token, g_user);
        g_syncTimer = 0.f;
        setRedeemStatus(
            r.message.empty() ? OBF("Key redeemed") : r.message.c_str(),
            false,
            3.5f);
        std::memset(g_key, 0, sizeof(g_key));
        return;
    }
    g_token = r.token;
    g_prodScroll = 0.f;
    g_user = r.user;
    if (g_user.products.empty() && !g_user.productsFromServer && !r.user.product.empty())
        g_user.products.push_back(entitlementFromUser(r.user));
    pruneExpiredProducts();
    resetLiveThumbs();
    g_auth.saveSession(g_token, g_user);
    g_view = View::LoggedIn;
    g_spinAlpha = 1.f;
    setStatus("", false);
    g_syncTimer = 8.f;
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
    if (got) {
        applyAuthResult(r);
        g_busy.store(false);
    }
}

void submitAuth() {
    if (g_busy.load())
        return;
    const std::string name = g_name;
    const std::string password = g_password;
    if (name.size() < 3 || password.empty()) {
        setStatus(OBF("Wrong name or password"), true);
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
            r.message = OBF("Request failed");
        }
        {
            std::lock_guard<std::mutex> lock(g_resultMu);
            g_pending = r;
            g_gotResult = true;
        }
    }).detach();
}

void submitRedeem() {
    if (g_redeemBusy.load() || g_token.empty())
        return;
    const std::string key = g_key;
    if (key.size() < 10) {
        setRedeemStatus(OBF("Invalid key"), true);
        return;
    }
    g_redeemBusy.store(true);
    setRedeemStatus("", false);
    const std::string token = g_token;
    std::thread([key, token] {
        AuthResult r;
        try {
            AuthService svc;
            r = svc.redeem(token, key);
        } catch (...) {
            r.ok = false;
            r.message = OBF("Redeem failed");
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

void wipeSensitiveMemory(const bool clearDiskSession) {
    SecureZeroMemory(g_name, sizeof(g_name));
    SecureZeroMemory(g_password, sizeof(g_password));
    SecureZeroMemory(g_key, sizeof(g_key));
    SecureZeroMemory(g_status, sizeof(g_status));
    SecureZeroMemory(g_redeemStatus, sizeof(g_redeemStatus));
    g_redeemError = false;
    g_redeemStatusTimer = 0.f;
    g_redeemOpen = false;
    SecureZeroMemory(g_playMsg, sizeof(g_playMsg));
    SecureZeroMemory(g_fivemPath, sizeof(g_fivemPath));
    g_token.clear();
    g_token.shrink_to_fit();
    g_user = AuthUser{};
    g_playProduct.clear();
    resetLiveThumbs();
    g_playBusy.store(false);
    g_busy.store(false);
    g_redeemBusy.store(false);
    if (clearDiskSession)
        g_auth.clearSession();
}

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

        ImGui::Begin("##loader", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        dl->AddRectFilled(wp, wp + ws, col32(c::window_bg_color));

        const bool busy = g_busy.load();
        const bool openingMain = (g_view == View::LoggedIn && g_expand < 0.995f);
        const bool loading = busy || openingMain;
        tickSpinner(loading);
        const bool showForm = (g_view == View::Form && !busy && g_spinAlpha < 0.2f);

        if (showForm) {
            if (font::esp_font)
                ImGui::PushFont(font::esp_font);

            ImGui::SetCursorPos(ImVec2(16.f, 12.f));
            if (font::brand_font)
                ImGui::PushFont(font::brand_font);
            ImGui::TextUnformatted(loaderBrand());
            if (font::brand_font)
                ImGui::PopFont();

            ImGui::Dummy(ImVec2(0.f, 8.f));
            const float inner_w = ImGui::GetContentRegionAvail().x;

            const float gap = 8.f;
            const float half = (inner_w - gap) * 0.5f;
            if (custom::Tab(OBF("Log in"), g_mode == AuthMode::Login, ImVec2(half, 32.f)))
                g_mode = AuthMode::Login;
            ImGui::SameLine(0.f, gap);
            if (custom::Tab(OBF("Sign up"), g_mode == AuthMode::SignUp, ImVec2(half, 32.f)))
                g_mode = AuthMode::SignUp;

            ImGui::Dummy(ImVec2(0.f, 10.f));
            field(OBF("Name"), OBF("name"), g_name, IM_ARRAYSIZE(g_name), inner_w);
            ImGui::Dummy(ImVec2(0.f, 6.f));
            field(OBF("Password"), OBF("pass"), g_password, IM_ARRAYSIZE(g_password), inner_w,
                ImGuiInputTextFlags_Password);

            ImGui::Dummy(ImVec2(0.f, 10.f));
            if (custom::Button(g_mode == AuthMode::Login ? OBF("Sign in") : OBF("Create account"),
                    ImVec2(inner_w, 36.f)))
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
            drawSpinner(dl, wp + ws * 0.5f, 15.f, g_spinAlpha);
        }

        if (g_view == View::LoggedIn && g_expand >= 0.995f && g_spinAlpha < 0.12f) {
            tickLiveProduct();
            tickRedeemStatusTimer();
            ImGui::SetCursorScreenPos(ImVec2(wp.x + 14.f, wp.y + 10.f));
            if (custom::Button(OBF("Redeem key"), ImVec2(112.f, 28.f)))
                openRedeemDialog();
            dl->AddRect(
                ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax(),
                IM_COL32(255, 255, 255, 32),
                8.f, 0, 1.f);
            if (hasActiveProduct())
                drawProductList(dl, wp, ws);
        }

        drawCloseX(dl);

        ImGui::End();

        if (g_view == View::LoggedIn && g_expand > 0.97f && g_redeemOpen) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                g_redeemOpen = false;
                setRedeemStatus("", false);
            }

            const ImVec2 box(340.f, 198.f);
            const ImVec2 p0 = wp + (ws - box) * 0.5f;
            const ImVec2 p1 = p0 + box;

            ImGui::SetNextWindowPos(wp, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ws, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.f);
            ImGui::SetNextWindowFocus();
            ImGui::Begin(OBF("##redeem_layer"), nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);

            ImDrawList* overlay = ImGui::GetWindowDrawList();
            overlay->AddRectFilled(wp, wp + ws, IM_COL32(0, 0, 0, 80));
            overlay->AddRectFilled(p0, p1, IM_COL32(18, 18, 20, 255), 10.f);
            overlay->AddRect(p0, p1, IM_COL32(70, 70, 76, 255), 10.f, 0, 1.f);

            ImGui::SetNextItemAllowOverlap();
            ImGui::SetCursorScreenPos(wp);
            if (ImGui::InvisibleButton(OBF("redeem_dim"), ws)) {
                const ImVec2 m = ImGui::GetIO().MousePos;
                const bool inside = m.x >= p0.x && m.x <= p1.x && m.y >= p0.y && m.y <= p1.y;
                if (!inside) {
                    g_redeemOpen = false;
                    setRedeemStatus("", false);
                }
            }

            ImGui::SetCursorScreenPos(p0 + ImVec2(16.f, 14.f));
            ImGui::BeginChild(OBF("redeem_box"), ImVec2(box.x - 32.f, box.y - 28.f), false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::TextUnformatted(OBF("Redeem key"));
            ImGui::Dummy(ImVec2(0.f, 8.f));
            textInput("redeemkey", g_key, IM_ARRAYSIZE(g_key), box.x - 32.f, 34.f);
            ImGui::Dummy(ImVec2(0.f, 10.f));
            const float btnGap = 8.f;
            const float btnW = (box.x - 32.f - btnGap) * 0.5f;
            ImGui::BeginDisabled(g_redeemBusy.load());
            if (custom::Button(OBF("Redeem"), ImVec2(btnW, 32.f)))
                g_wantRedeem = true;
            ImGui::EndDisabled();
            ImGui::SameLine(0.f, btnGap);
            if (custom::Button(OBF("Close"), ImVec2(btnW, 32.f))) {
                g_redeemOpen = false;
                setRedeemStatus("", false);
            }
            ImGui::Dummy(ImVec2(0.f, 8.f));
            ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + box.x - 32.f);
            const ImVec4 redeemTextCol = g_redeemError
                ? ImVec4(0.91f, 0.28f, 0.28f, 1.f)
                : (g_redeemStatus[0]
                    ? ImVec4(0.55f, 0.88f, 0.58f, 1.f)
                    : utils::ImColorToImVec4(c::text::label::default));
            ImGui::PushStyleColor(ImGuiCol_Text, redeemTextCol);
            ImGui::TextUnformatted(g_redeemStatus[0] ? g_redeemStatus : " ");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::EndChild();

            drawCloseX(overlay);
            ImGui::End();
        }

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
        setStatus(OBF("Wrong name or password"), true);
    }
}

} // namespace loader_ui
