#include "app_window.hpp"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <windowsx.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

AppWindow g_app;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

namespace {

void createRenderTarget(AppWindow& app) {
    ID3D11Texture2D* backBuffer = nullptr;
    app.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    app.device->CreateRenderTargetView(backBuffer, nullptr, &app.renderTarget);
    backBuffer->Release();
}

void cleanupRenderTarget(AppWindow& app) {
    if (app.renderTarget) {
        app.renderTarget->Release();
        app.renderTarget = nullptr;
    }
}

bool createDevice(AppWindow& app) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = app.hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    const HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
        D3D11_SDK_VERSION, &sd, &app.swapChain, &app.device, &featureLevel, &app.context);

    if (FAILED(hr))
        return false;

    createRenderTarget(app);
    return true;
}

} // namespace

LRESULT CALLBACK AppWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (pt.y >= 0 && pt.y < 46 && pt.x >= 128 && pt.x < (rc.right - 44))
            return HTCAPTION;
    }

    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    AppWindow* app = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_SIZE:
        if (app && app->device && wParam != SIZE_MINIMIZED) {
            app->handleResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool AppWindow::create(HINSTANCE instance) {
    width = 360;
    height = 356;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = AppWindow::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"LoaderAuthWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    const int sx = GetSystemMetrics(SM_CXSCREEN);
    const int sy = GetSystemMetrics(SM_CYSCREEN);
    const int x = (sx - width) / 2;
    const int y = (sy - height) / 2;

    hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_TOPMOST,
        wc.lpszClassName,
        L"Loader",
        WS_POPUP | WS_VISIBLE,
        x, y, width, height,
        nullptr, nullptr, instance, nullptr);

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    const int roundPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &roundPref, sizeof(roundPref));
    const COLORREF border = RGB(72, 72, 72);
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));

    if (!createDevice(*this))
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, context);
    return true;
}

void AppWindow::destroy() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    cleanupRenderTarget(*this);
    if (swapChain) { swapChain->Release(); swapChain = nullptr; }
    if (context) { context->Release(); context = nullptr; }
    if (device) { device->Release(); device = nullptr; }
    if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
}

void AppWindow::handleResize(UINT w, UINT h) {
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    cleanupRenderTarget(*this);
    swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    createRenderTarget(*this);
}

void AppWindow::setClientSizeCentered(int w, int h) {
    if (!hwnd || w < 1 || h < 1)
        return;
    if (w == width && h == height)
        return;
    const int sx = GetSystemMetrics(SM_CXSCREEN);
    const int sy = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hwnd, HWND_TOPMOST, (sx - w) / 2, (sy - h) / 2, w, h, SWP_NOACTIVATE);
}

bool AppWindow::beginFrame() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
            return false;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void AppWindow::endFrame() {
    ImGui::Render();
    const float clear[4] = { 8.f / 255.f, 8.f / 255.f, 8.f / 255.f, 1.f };
    context->OMSetRenderTargets(1, &renderTarget, nullptr);
    context->ClearRenderTargetView(renderTarget, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swapChain->Present(1, 0);
}
