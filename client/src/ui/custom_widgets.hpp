#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#include "imgui_internal.h"

namespace custom {
bool Button(const char* label, const ImVec2& size_arg = ImVec2(0, 0), ImGuiButtonFlags extra_flags = 0);
bool Tab(const char* label, bool selected, const ImVec2& size_arg);
}
