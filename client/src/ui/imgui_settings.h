#include "imgui.h"
#include "font_defines.h"
#include <string>
// https://discord.authguards.com/
// https://authguards.com/
inline ImVec4 rgba(int r, int g, int b, float a) {
	return ImVec4(
		(float)r / 255.0f,
		(float)g / 255.0f,
		(float)b / 255.0f,
		a
	);
}

namespace font
{
	inline ImFont* icomoon_logo = nullptr;
	inline ImFont* esp_font = nullptr;
	inline ImFont* brand_font = nullptr;
	inline ImFont* regular_m = nullptr;
	inline ImFont* regular_l = nullptr;
	inline ImFont* icomoon_page = nullptr;
	inline ImFont* inter_semibold = nullptr;
	inline ImFont* bold_font = nullptr;
	inline ImFont* s_inter_semibold = nullptr;
	inline ImFont* inter_medium = nullptr;
	inline ImFont* icon_notify = nullptr;
}

namespace utils
{
	inline ImColor GetColorWithAlpha(ImColor color, float alpha)
	{
		return ImColor(color.Value.x, color.Value.y, color.Value.z, alpha);
	}

	inline ImVec2 center_text(ImVec2 min, ImVec2 max, const char* text)
	{
		return min + (max - min) / 2 - ImGui::CalcTextSize(text) / 2;
	}

	inline ImColor GetDarkColor(const ImColor& color)
	{
		float r, g, b, a;
		r = color.Value.x;
		g = color.Value.y;
		b = color.Value.z;
		a = color.Value.w;

		const float darkPercentage = 0.55f;
		return ImColor(r * darkPercentage, g * darkPercentage, b * darkPercentage, a);
	}
	inline ImVec4 ImColorToImVec4(const ImColor& color)
	{
		return ImVec4(color.Value.x, color.Value.y, color.Value.z, color.Value.w);
	}

}

inline namespace c
{
	// Global menu scale — tweak this to resize the whole UI (window, fonts, widgets).
	inline namespace ui
	{
		inline constexpr float scale = 0.80f;
		inline float S(float v) { return v * scale; }
	}

	// Solid dark theme — black / white / gray only (no glass / transparency)
	inline ImColor main_color = ImColor(235, 235, 235, 255);
	inline ImColor second_color = ImColor(110, 110, 110, 255);
	inline ImColor accent_bar = ImColor(58, 58, 58, 255); // header rule, subtle chrome

	inline ImColor window_bg_color = rgba(8, 8, 8, 1);
	inline ImColor header_bg = rgba(12, 12, 12, 1);
	inline ImColor sidebar_bg = rgba(14, 14, 14, 1);
	inline ImColor content_bg = rgba(18, 18, 18, 1);
	inline ImColor border_color = rgba(42, 42, 42, 1);

	inline ImColor background_color = rgba(22, 22, 22, 1);
	inline ImColor stroke_color = rgba(55, 55, 55, 1);

	inline ImVec4 separator = ImColor(55, 55, 55, 255);


	inline namespace anim
	{
		inline float speed;
		inline ImColor active(230, 230, 230, 255);
		inline ImColor default(18, 18, 18, 255);
	}

	inline namespace bg
	{
		inline ImVec4 background = rgba(8, 8, 8, 1);
		inline ImVec2 size = ImVec2(ui::S(960.f), ui::S(580.f));
		inline float rounding = ui::S(18.f);
	}

	// Single geometry source — chrome plates + ImGui children must match these exactly.
	inline namespace layout
	{
		inline float gap = ui::S(12.f);
		inline float header_h = ui::S(48.f);
		inline float sidebar_w = ui::S(168.f);
		inline float title_cap = ui::S(40.f);
		inline float sidebar_inner = ui::S(8.f);
		inline float rounding = ui::S(12.f);

		inline float tab_row_h = ui::S(40.f);
		inline float tab_icon_x = ui::S(42.f);
		inline float tab_icon_cx = ui::S(22.f);
		inline float tab_icon_r = ui::S(9.4f);
		inline float tab_spacing_y = ui::S(6.f);
		inline float sidebar_min_h = ui::S(120.f);

		inline float checkbox_row_h = ui::S(26.f);
		inline float toggle_w = ui::S(42.f);
		inline float toggle_h = ui::S(24.f);
		inline float toggle_thumb_r = ui::S(9.f);

		inline float keybind_row_h = ui::S(22.f);
		inline float slider_row_h = ui::S(32.f);
		inline float slider_track_h = ui::S(8.f);
		// Thumb radius derived from track — slightly larger than rail for visibility.
		inline float slider_thumb_r() {
			return ImMax(slider_track_h * 0.68f, ui::S(10.f));
		}
		inline float slider_row_extra() {
			return slider_thumb_r() * 2.f + ui::S(4.f);
		}
		inline float color_row_h = ui::S(20.f);
		inline float btn_h = ui::S(42.f);

		inline float panel_pad_x = ui::S(14.f);
		inline float panel_pad_y = ui::S(10.f);
		inline float child_pad_x = ui::S(16.f);
		inline float child_pad_y = ui::S(14.f);
		inline float item_spacing = ui::S(9.f);

		inline float body_top() { return gap + header_h + gap; }
		inline float content_left() { return gap + sidebar_w + gap; }
		inline float content_w() { return bg::size.x - content_left() - gap; }
		inline float content_h() { return bg::size.y - body_top() - gap; }
		inline float sidebar_h() { return content_h(); }
	}

	inline namespace child
	{
		inline ImVec4 background = rgba(24, 24, 24, 1);
		inline ImVec4 stroke = ImColor(48, 48, 48, 255);
		inline float rounding = ui::S(12.f);
	}

	namespace page
	{
		inline ImVec4 background_active = ImColor(36, 36, 36);
		inline ImVec4 background = ImColor(18, 18, 18);

		inline ImVec4 text_hov = ImColor(220, 220, 220);
		inline ImVec4 text = ImColor(160, 160, 160);

		inline float rounding = ui::S(4.f);
	}

	inline namespace elements
	{
		inline ImVec4 background_hovered = ImColor(34, 34, 34);
		inline ImVec4 background = ImColor(26, 26, 26);
		inline float rounding = ui::S(4.f);
	}

	inline namespace checkbox
	{
		inline ImVec4 mark = ImColor(230, 230, 230, 255);
	}

	inline namespace text
	{
		inline namespace label
		{
			inline ImColor active = ImColor(240, 240, 240, 255);
			inline ImColor hovered = ImColor(205, 205, 205, 255);
			inline ImColor default = ImColor(150, 150, 150, 255);
		}

		inline namespace description
		{
			inline ImColor active = ImColor(165, 165, 165, 255);
			inline ImColor hovered = ImColor(135, 135, 135, 255);
			inline ImColor default = ImColor(105, 105, 105, 255);
		}

		inline ImVec4 text_active = ImColor(240, 240, 240);
		inline ImVec4 text_hov = ImColor(205, 205, 205);
		inline ImVec4 text = ImColor(150, 150, 150);
	}

	// LianFlow draw uses c::label::*
	namespace label = text::label;

	// Re-apply every frame so stale blue palette values cannot persist.
	inline void enforce_mono_theme()
	{
		main_color = ImColor(235, 235, 235, 255);
		second_color = ImColor(110, 110, 110, 255);
		accent_bar = ImColor(58, 58, 58, 255);
		anim::active = ImColor(220, 220, 220, 255);
		checkbox::mark = ImVec4(0.92f, 0.92f, 0.92f, 1.f);
	}
}

inline float page_offset;      // legacy (unused — fade replaces slide)
inline float page_alpha = 1.f; // tab content fade 0..1
inline int page_fade_phase = 0; // 0 idle, 1 fade-out, 2 fade-in
inline bool page_is_changing;
inline std::string wanted_category;
inline int wanted_idx;
