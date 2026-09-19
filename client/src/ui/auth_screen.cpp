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
ID3D11ShaderResourceView* g_fivemBanner = nullptr;
int g_fivemBannerW = 0;
int g_fivemBannerH = 0;
ID3D11ShaderResourceView* g_liveBanner = nullptr;
int g_liveBannerW = 0;
int g_liveBannerH = 0;
std::string g_loadedThumbVer;
std::atomic<bool> g_syncBusy{false};
float g_syncTimer = 8.f;
std::mutex g_thumbBytesMu;
std::string g_thumbBytes;
std::string g_thumbBytesVer;
bool g_thumbBytesReady = false;

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

void launchFiveM();
bool hasActiveProduct();
void tickLiveProduct();

void launchFiveM() {
    if (g_playBusy.load())
        return;
    if (!hasActiveProduct())
        return;
    const std::string token = g_token;
    const std::string product = g_user.product;
    const std::string ver = g_user.fileVersion.empty() ? OBF("live") : g_user.fileVersion;
    std::string name = g_user.fileName.empty() ? OBF("product.exe") : g_user.fileName;
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
            const std::wstring dir =
                std::wstring(app) + OBFW(L"\\Loader\\products\\FiveM\\") + utf8ToWide(ver);
            CreateDirectoryW((std::wstring(app) + OBFW(L"\\Loader")).c_str(), nullptr);
            CreateDirectoryW((std::wstring(app) + OBFW(L"\\Loader\\products")).c_str(), nullptr);
            CreateDirectoryW((std::wstring(app) + OBFW(L"\\Loader\\products\\FiveM")).c_str(), nullptr);
            CreateDirectoryW(dir.c_str(), nullptr);
            dest = dir + L"\\" + utf8ToWide(name);
            {
                std::snprintf(g_playMsg, sizeof(g_playMsg), "%s", OBF("Downloading..."));
                const HttpResponse resp =
                    HttpClient::request(OBFW(L"GET"), OBFW(L"/auth/product-file"), {}, token);
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

void formatRemain(char* out, size_t cap) {
    if (!out || cap < 8)
        return;
    if (g_user.lifetime) {
        std::snprintf(out, cap, "%s", OBF("Lifetime"));
        return;
    }
    const time_t exp = parseExpiresUtc(g_user.expires);
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

bool hasActiveProduct() {
    if (g_user.product.empty())
        return false;
    if (g_user.lifetime)
        return true;
    const time_t exp = parseExpiresUtc(g_user.expires);
    if (exp <= 0 || exp <= time(nullptr)) {
        g_user.product.clear();
        g_user.fileName.clear();
        g_user.fileVersion.clear();
        g_user.thumbVersion.clear();
        g_auth.saveSession(g_token, g_user);
        return false;
    }
    return true;
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

void applyPendingThumb() {
    std::string bytes, ver;
    {
        std::lock_guard<std::mutex> lock(g_thumbBytesMu);
        if (!g_thumbBytesReady)
            return;
        bytes.swap(g_thumbBytes);
        ver.swap(g_thumbBytesVer);
        g_thumbBytesReady = false;
    }
    if (bytes.empty())
        return;
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;
    if (!createTextureFromBytes(bytes.data(), bytes.size(), &srv, &w, &h))
        return;
    if (g_liveBanner)
        g_liveBanner->Release();
    g_liveBanner = srv;
    g_liveBannerW = w;
    g_liveBannerH = h;
    g_loadedThumbVer = ver;
}

void tickLiveProduct() {
    applyPendingThumb();
    if (g_view != View::LoggedIn || g_token.empty() || g_syncBusy.load())
        return;
    g_syncTimer += ImGui::GetIO().DeltaTime;
    const bool needThumb = hasActiveProduct() && !g_user.thumbVersion.empty() && g_user.thumbVersion != g_loadedThumbVer;
    if (!needThumb && g_syncTimer < 8.f)
        return;
    g_syncTimer = 0.f;
    g_syncBusy.store(true);
    const std::string token = g_token;
    const std::string loaded = g_loadedThumbVer;
    std::thread([token, loaded] {
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
        if (r.ok && !r.user.product.empty() && !r.user.thumbVersion.empty() && r.user.thumbVersion != loaded) {
            try {
                const HttpResponse img = HttpClient::request(OBFW(L"GET"), OBFW(L"/auth/product-thumb"), {}, token);
                if (img.status >= 200 && img.status < 300 && !img.body.empty()) {
                    std::lock_guard<std::mutex> lock(g_thumbBytesMu);
                    g_thumbBytes = img.body;
                    g_thumbBytesVer = r.user.thumbVersion;
                    g_thumbBytesReady = true;
                }
            } catch (...) {
            }
        }
        g_syncBusy.store(false);
    }).detach();
}

void coverUv(float boxW, float boxH, float imgW, float imgH, ImVec2& uv0, ImVec2& uv1) {
    uv0 = ImVec2(0.f, 0.f);
    uv1 = ImVec2(1.f, 1.f);
    if (boxW < 1.f || boxH < 1.f || imgW < 1.f || imgH < 1.f)
        return;
    const float boxA = boxW / boxH;
    const float imgA = imgW / imgH;
    if (imgA > boxA) {
        const float vis = boxA / imgA;
        uv0.x = (1.f - vis) * 0.5f;
        uv1.x = uv0.x + vis;
    } else {
        const float vis = imgA / boxA;
        uv0.y = (1.f - vis) * 0.5f;
        uv1.y = uv0.y + vis;
    }
    const float du = 0.75f / imgW;
    const float dv = 0.75f / imgH;
    uv0.x += du;
    uv0.y += dv;
    uv1.x -= du;
    uv1.y -= dv;
}

void drawFiveMProduct(ImDrawList* dl, const ImVec2& wp, const ImVec2& ws) {
    ensureFiveMBanner();
    const float cardW = ws.x - 32.f;
    const float cardH = 108.f;
    const float rnd = 12.f;
    const ImDrawFlags roundAll = ImDrawFlags_RoundCornersAll;

    const float x0 = floorf(wp.x + 16.f);
    const float y0 = floorf(wp.y + 58.f);
    const float x1 = x0 + floorf(cardW);
    const float y1 = y0 + floorf(cardH);
    const ImVec2 p0(x0, y0);
    const ImVec2 p1(x1, y1);
    const float iw = x1 - x0;
    const float ih = y1 - y0;

    dl->AddRectFilled(p0, p1, IM_COL32(10, 10, 12, 255), rnd, roundAll);

    if (g_fivemBanner || g_liveBanner) {
        ID3D11ShaderResourceView* tex = g_liveBanner ? g_liveBanner : g_fivemBanner;
        const float tw = g_liveBanner ? (float)g_liveBannerW : (float)g_fivemBannerW;
        const float th = g_liveBanner ? (float)g_liveBannerH : (float)g_fivemBannerH;
        ImVec2 uv0, uv1;
        coverUv(iw, ih, tw, th, uv0, uv1);
        dl->AddImageRounded((ImTextureID)tex, p0, p1, uv0, uv1, IM_COL32(255, 255, 255, 255), rnd, roundAll);

        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 118), rnd, roundAll);
    }

    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 42), rnd, roundAll, 1.f);

    const float x = p0.x;
    const float y = p0.y;
    const ImVec2 title(x + 18.f, y + 16.f);
    if (font::brand_font)
        ImGui::PushFont(font::brand_font);
    ImGui::SetCursorScreenPos(ImVec2(title.x + 1.f, title.y + 1.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 0.f, 0.f, 0.72f));
    ImGui::TextUnformatted(OBF("FiveM"));
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(title);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    ImGui::TextUnformatted(OBF("FiveM"));
    ImGui::PopStyleColor();
    if (font::brand_font)
        ImGui::PopFont();

    char remain[64]{};
    formatRemain(remain, sizeof(remain));
    const char* sub = g_playMsg[0] ? g_playMsg : remain;
    ImGui::SetCursorScreenPos(ImVec2(x + 18.f, y + 44.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.90f, 0.93f, 0.92f));
    ImGui::TextUnformatted(sub);
    ImGui::PopStyleColor();

    const float box = 46.f;
    const float boxR = 10.f;
    const ImVec2 b0(floorf(p1.x - 16.f - box), floorf((p0.y + p1.y) * 0.5f - box * 0.5f));
    const ImVec2 b1(b0.x + box, b0.y + box);
    ImGui::SetCursorScreenPos(b0);
    ImGui::BeginDisabled(g_playBusy.load());
    if (ImGui::InvisibleButton(OBF("play_fivem"), ImVec2(box, box)))
        launchFiveM();
    ImGui::EndDisabled();
    const bool hov = ImGui::IsItemHovered();
    dl->AddRectFilled(b0, b1, hov ? IM_COL32(58, 60, 66, 255) : IM_COL32(42, 44, 50, 255), boxR, roundAll);
    dl->AddRect(b0, b1, hov ? IM_COL32(210, 212, 218, 70) : IM_COL32(255, 255, 255, 38), boxR, roundAll, 1.f);

    const ImVec2 pc((b0.x + b1.x) * 0.5f + 1.35f, (b0.y + b1.y) * 0.5f);
    const float tw = 11.2f;
    const float th = 12.8f;
    dl->PathLineTo(ImVec2(pc.x - tw * 0.42f, pc.y - th * 0.5f));
    dl->PathLineTo(ImVec2(pc.x + tw * 0.62f, pc.y));
    dl->PathLineTo(ImVec2(pc.x - tw * 0.42f, pc.y + th * 0.5f));
    dl->PathFillConvex(hov ? IM_COL32(248, 248, 250, 255) : IM_COL32(232, 233, 238, 255));
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
        setStatus(r.message.empty() ? OBF("Wrong name or password") : r.message.c_str(), true);
        return;
    }
    if (r.liveSync) {
        if (r.ok) {
            g_user.product = r.user.product;
            g_user.lifetime = r.user.lifetime;
            g_user.expires = r.user.expires;
            if (!r.user.fileName.empty())
                g_user.fileName = r.user.fileName;
            if (!r.user.fileVersion.empty())
                g_user.fileVersion = r.user.fileVersion;
            g_user.thumbVersion = r.user.thumbVersion;
            g_auth.saveSession(g_token, g_user);
        }
        return;
    }
    if (g_view == View::LoggedIn) {
        if (!r.user.product.empty())
            g_user.product = r.user.product;
        g_user.lifetime = r.user.lifetime;
        g_user.expires = r.user.expires;
        if (!r.user.fileName.empty())
            g_user.fileName = r.user.fileName;
        if (!r.user.fileVersion.empty())
            g_user.fileVersion = r.user.fileVersion;
        if (!r.user.thumbVersion.empty())
            g_user.thumbVersion = r.user.thumbVersion;
        g_auth.saveSession(g_token, g_user);
        g_syncTimer = 8.f;
        setStatus(r.message.empty() ? OBF("Key redeemed") : r.message.c_str(), false);
        std::memset(g_key, 0, sizeof(g_key));
        g_redeemOpen = false;
        return;
    }
    g_token = r.token;
    g_user = r.user;
    g_auth.saveSession(g_token, g_user);
    g_view = View::LoggedIn;
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
    if (got)
        applyAuthResult(r);
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
        g_busy.store(false);
    }).detach();
}

void submitRedeem() {
    if (g_redeemBusy.load() || g_token.empty())
        return;
    const std::string key = g_key;
    if (key.size() < 10) {
        setStatus(OBF("Invalid key"), true);
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
    SecureZeroMemory(g_playMsg, sizeof(g_playMsg));
    SecureZeroMemory(g_fivemPath, sizeof(g_fivemPath));
    g_token.clear();
    g_token.shrink_to_fit();
    g_user = AuthUser{};
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

        ImGui::Begin(OBF("auth"), nullptr,
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
            drawSpinner(dl, wp + ws * 0.5f, ImLerp(12.f, 18.f, g_expand), g_spinAlpha);
        }

        if (g_view == View::LoggedIn && g_expand > 0.97f && g_spinAlpha < 0.25f) {
            tickLiveProduct();
            ImGui::SetCursorScreenPos(ImVec2(wp.x + 14.f, wp.y + 10.f));
            if (custom::Button(OBF("Redeem key"), ImVec2(112.f, 28.f)))
                g_redeemOpen = true;

            if (hasActiveProduct())
                drawFiveMProduct(dl, wp, ws);

            if (g_redeemOpen) {
                const ImVec2 box(340.f, 198.f);
                const ImVec2 p0 = wp + (ws - box) * 0.5f;
                const ImVec2 p1 = p0 + box;
                dl->AddRectFilled(p0, p1, IM_COL32(14, 14, 14, 255), 10.f);
                dl->AddRect(p0, p1, IM_COL32(48, 48, 48, 255), 10.f, 0, 1.f);
                ImGui::SetCursorScreenPos(p0 + ImVec2(16.f, 14.f));
                ImGui::BeginGroup();
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
                if (custom::Button(OBF("Close"), ImVec2(btnW, 32.f)))
                    g_redeemOpen = false;
                ImGui::Dummy(ImVec2(0.f, 8.f));
                ImGui::PushTextWrapPos(p0.x + box.x - 16.f);
                ImGui::PushStyleColor(ImGuiCol_Text, g_error
                    ? ImVec4(0.91f, 0.28f, 0.28f, 1.f)
                    : utils::ImColorToImVec4(c::text::label::default));
                ImGui::TextUnformatted(g_status[0] ? g_status : " ");
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
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
        setStatus(OBF("Wrong name or password"), true);
    }
}

} // namespace loader_ui
