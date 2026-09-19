#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

struct AppWindow {
    HWND hwnd = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTarget = nullptr;

    int width = 520;
    int height = 640;

    bool create(HINSTANCE instance);
    void destroy();
    bool beginFrame();
    void endFrame();
    void handleResize(UINT width, UINT height);

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

extern AppWindow g_app;
