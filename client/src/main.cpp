#include "app_window.hpp"
#include "auth_screen.hpp"
#include "imgui_impl_dx11.h"
#include <windows.h>
#include <thread>

namespace {
constexpr ULONGLONG kHardSessionMs = 3ull * 60ull * 1000ull;
ULONGLONG g_bootTick = 0;

void enforceHardTimeout() {
    if (g_bootTick && GetTickCount64() - g_bootTick >= kHardSessionMs)
        ExitProcess(0);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    g_bootTick = GetTickCount64();
    std::thread([] {
        Sleep(static_cast<DWORD>(kHardSessionMs));
        ExitProcess(0);
    }).detach();

    if (!g_app.create(instance))
        return 1;

    loader_ui::initTheme();
    ImGui_ImplDX11_CreateDeviceObjects();

    while (g_app.beginFrame()) {
        enforceHardTimeout();
        try {
            loader_ui::drawAuthScreen();
        } catch (...) {
        }
        enforceHardTimeout();
        g_app.endFrame();
    }

    g_app.destroy();
    return 0;
}
