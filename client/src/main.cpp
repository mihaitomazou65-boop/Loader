#include "app_window.hpp"
#include "auth_screen.hpp"
#include "imgui_impl_dx11.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!g_app.create(instance))
        return 1;

    loader_ui::initTheme();
    ImGui_ImplDX11_CreateDeviceObjects();

    while (g_app.beginFrame()) {
        try {
            loader_ui::drawAuthScreen();
        } catch (...) {
        }
        g_app.endFrame();
    }

    g_app.destroy();
    return 0;
}
