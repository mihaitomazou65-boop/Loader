#include "custom_widgets.hpp"
#include "imgui_settings.h"

#include <map>

using namespace ImGui;

namespace custom {

struct button_state {
    ImVec4 background, text;
    float outline;
};

bool Button(const char* label, const ImVec2& size_arg, ImGuiButtonFlags extra_flags) {
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const ImVec2 pos = window->DC.CursorPos;

    ImVec2 size = CalcItemSize(size_arg, label_size.x, label_size.y);
    const ImRect bb(pos, pos + size);

    ItemSize(size, 0.f);
    if (!ItemAdd(bb, id)) return false;

    bool hovered, held, pressed = ButtonBehavior(bb, id, &hovered, &held, extra_flags);

    static std::map<ImGuiID, button_state> anim;
    auto it_anim = anim.find(id);
    if (it_anim == anim.end()) {
        anim.insert({ id, button_state{} });
        it_anim = anim.find(id);
    }

    const ImVec4 bg_idle = c::elements::background;
    const ImVec4 bg_hov = c::elements::background_hovered;
    const ImVec4 bg_act = ImVec4(0.42f, 0.42f, 0.42f, 0.45f);
    const ImVec4 target_bg = held ? bg_act : hovered ? bg_hov : bg_idle;
    const ImVec4 target_text = held ? c::text::label::active : hovered ? c::text::label::hovered : c::text::label::default;
    const float target_outline = held ? 0.55f : hovered ? 0.38f : 0.18f;

    it_anim->second.background = ImLerp(it_anim->second.background, target_bg, g.IO.DeltaTime * 22.f);
    it_anim->second.text = ImLerp(it_anim->second.text, target_text, g.IO.DeltaTime * 22.f);
    it_anim->second.outline = ImLerp(it_anim->second.outline, target_outline, g.IO.DeltaTime * 22.f);

    GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, GetColorU32(it_anim->second.background), c::page::rounding);
    GetWindowDrawList()->AddRect(bb.Min, bb.Max,
        GetColorU32(ImVec4(c::main_color.Value.x, c::main_color.Value.y, c::main_color.Value.z,
            it_anim->second.outline * style.Alpha)),
        c::page::rounding, 0, 1.25f);

    PushClipRect(bb.Min, bb.Max, true);
    GetWindowDrawList()->AddText(
        ImVec2(bb.Min.x + (size.x - CalcTextSize(label).x) * 0.5f,
            bb.Min.y + (size.y - CalcTextSize(label).y) * 0.5f),
        GetColorU32(it_anim->second.text), label);
    PopClipRect();

    return pressed;
}

} // namespace custom
