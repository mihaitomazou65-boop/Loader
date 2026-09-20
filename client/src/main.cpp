#include "app_window.hpp"
#include "auth_screen.hpp"
#include "protect.hpp"
#include "imgui_impl_dx11.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\LoaderAppSingleton");
    if (!single || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (single)
            CloseHandle(single);
        if (HWND existing = FindWindowW(L"LoaderAuthWindow", nullptr)) {
            ShowWindow(existing, SW_SHOW);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    protect::init();

    if (!g_app.create(instance))
        return 1;

    loader_ui::initTheme();
    ImGui_ImplDX11_CreateDeviceObjects();

    while (g_app.beginFrame()) {
        protect::tick();
        try {
            loader_ui::drawAuthScreen();
        } catch (...) {
        }
        g_app.endFrame();
    }

    loader_ui::wipeSensitiveMemory();
    g_app.destroy();
    CloseHandle(single);
    return 0;
}
