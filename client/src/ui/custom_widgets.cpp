#include "custom_widgets.hpp"
#include "imgui_settings.h"

#include <map>

using namespace ImGui;

namespace custom {

struct button_state {
    ImVec4 background = ImVec4(0, 0, 0, 0);
    ImVec4 text = ImVec4(0.6f, 0.6f, 0.6f, 1.f);
};

static ImVec2 ExactSize(const ImVec2& size_arg, const ImVec2& label_size, float default_h) {
    ImVec2 size = size_arg;
    if (size.x <= 0.f) size.x = label_size.x + 24.f;
    if (size.y <= 0.f) size.y = default_h;
    return size;
}

bool Button(const char* label, const ImVec2& size_arg, ImGuiButtonFlags extra_flags) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 size = ExactSize(size_arg, label_size, 36.f);
    const ImRect bb(pos, pos + size);

    ItemSize(size);
    if (!ItemAdd(bb, id)) return false;

    bool hovered, held;
    const bool pressed = ButtonBehavior(bb, id, &hovered, &held, extra_flags);

    static std::map<ImGuiID, button_state> anim;
    auto& st = anim[id];

    const ImVec4 bg_idle = c::elements::background;
    const ImVec4 bg_hov = c::elements::background_hovered;
    const ImVec4 bg_act = ImVec4(0.22f, 0.22f, 0.22f, 1.f);
    const ImVec4 target_bg = held ? bg_act : hovered ? bg_hov : bg_idle;
    const ImVec4 target_text = held ? utils::ImColorToImVec4(c::text::label::active)
        : hovered ? utils::ImColorToImVec4(c::text::label::hovered)
        : utils::ImColorToImVec4(c::text::label::default);

    st.background = ImLerp(st.background, target_bg, g.IO.DeltaTime * 22.f);
    st.text = ImLerp(st.text, target_text, g.IO.DeltaTime * 22.f);

    GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, GetColorU32(st.background), 8.f);
    GetWindowDrawList()->AddText(
        ImVec2(bb.Min.x + (size.x - label_size.x) * 0.5f, bb.Min.y + (size.y - label_size.y) * 0.5f),
        GetColorU32(st.text), label);

    return pressed;
}

bool Tab(const char* label, bool selected, const ImVec2& size_arg) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const ImVec2 pos = window->DC.CursorPos;
    const ImVec2 size = ExactSize(size_arg, label_size, 32.f);
    const ImRect bb(pos, pos + size);

    ItemSize(size);
    if (!ItemAdd(bb, id)) return false;

    bool hovered, held;
    const bool pressed = ButtonBehavior(bb, id, &hovered, &held);

    static std::map<ImGuiID, button_state> anim;
    auto& st = anim[id];

    const ImVec4 bg_off = utils::ImColorToImVec4(c::sidebar_bg);
    const ImVec4 bg_on = c::page::background_active;
    const ImVec4 target_bg = selected ? bg_on : (hovered ? c::elements::background_hovered : bg_off);
    const ImVec4 target_text = selected ? utils::ImColorToImVec4(c::text::label::active)
        : hovered ? utils::ImColorToImVec4(c::text::label::hovered)
        : utils::ImColorToImVec4(c::text::label::default);

    st.background = ImLerp(st.background, target_bg, g.IO.DeltaTime * 22.f);
    st.text = ImLerp(st.text, target_text, g.IO.DeltaTime * 22.f);

    GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, GetColorU32(st.background), 8.f);
    GetWindowDrawList()->AddText(
        ImVec2(bb.Min.x + (size.x - label_size.x) * 0.5f, bb.Min.y + (size.y - label_size.y) * 0.5f),
        GetColorU32(st.text), label);

    return pressed;
}

} // namespace custom
