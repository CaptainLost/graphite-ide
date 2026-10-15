#include "corepch.h"

#include "Core/ImGui/Theme.h"

#include "Core/ImGui/ImGuiColors.h"
#include <imgui.h>

namespace Core {

	namespace {
		constexpr ImVec4 kBackground{0.106f, 0.114f, 0.129f, 1.0f};
		constexpr ImVec4 kSurface{0.145f, 0.157f, 0.176f, 1.0f};
		constexpr ImVec4 kSurfaceHovered{0.196f, 0.212f, 0.239f, 1.0f};
		constexpr ImVec4 kSurfaceActive{0.243f, 0.263f, 0.298f, 1.0f};
		constexpr ImVec4 kAccent{0.259f, 0.522f, 0.957f, 1.0f};
		constexpr ImVec4 kText{0.839f, 0.859f, 0.886f, 1.0f};
		constexpr ImVec4 kTextDisabled{0.482f, 0.510f, 0.553f, 1.0f};

		constexpr Color kBorder{0.204f, 0.220f, 0.247f, 1.0f};

		constexpr Color FromImVec4(const ImVec4& color)
		{
			return Color{color.x, color.y, color.z, color.w};
		}

		constexpr float kLightBackgroundLuminance = 0.5f;
		constexpr float kRedLuminanceWeight = 0.2126f;
		constexpr float kGreenLuminanceWeight = 0.7152f;
		constexpr float kBlueLuminanceWeight = 0.0722f;
		constexpr float kDockingPreviewAlpha = 0.45f;

		constexpr bool IsLight(const Color& background)
		{
			const float luminance = kRedLuminanceWeight * background.R + kGreenLuminanceWeight * background.G +
									kBlueLuminanceWeight * background.B;

			return luminance > kLightBackgroundLuminance;
		}
	}

	ChromePalette Theme::DefaultPalette()
	{
		ChromePalette palette;

		palette.Background = FromImVec4(kBackground);
		palette.Surface = FromImVec4(kSurface);
		palette.SurfaceHovered = FromImVec4(kSurfaceHovered);
		palette.SurfaceActive = FromImVec4(kSurfaceActive);
		palette.Accent = FromImVec4(kAccent);
		palette.Text = FromImVec4(kText);
		palette.TextDisabled = FromImVec4(kTextDisabled);
		palette.Border = kBorder;

		return palette;
	}

	Color Theme::GetBorderColor()
	{
		return DefaultPalette().Border;
	}

	void Theme::SetMetrics(ImGuiStyle& style)
	{
		style.WindowRounding = 4.0f;
		style.ChildRounding = 4.0f;
		style.FrameRounding = 3.0f;
		style.PopupRounding = 4.0f;
		style.ScrollbarRounding = 6.0f;
		style.TabRounding = 3.0f;
		style.GrabRounding = 3.0f;
		style.WindowBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;
		style.WindowPadding = ImVec2(8.0f, 8.0f);
		style.FramePadding = ImVec2(8.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 5.0f);
		style.WindowMenuButtonPosition = ImGuiDir_None;
	}

	void Theme::Apply(const ChromePalette& palette)
	{
		// WARNING: about forty ImGuiCol_ entries are not set below and keep the base style's colours, so the base must match the palette's luminance.
		if (IsLight(palette.Background))
		{
			ImGui::StyleColorsLight();
		}
		else
		{
			ImGui::StyleColorsDark();
		}

		ImGuiStyle& style = ImGui::GetStyle();

		const ImVec4 background = ToImVec4(palette.Background);
		const ImVec4 surface = ToImVec4(palette.Surface);
		const ImVec4 surfaceHovered = ToImVec4(palette.SurfaceHovered);
		const ImVec4 surfaceActive = ToImVec4(palette.SurfaceActive);
		const ImVec4 accent = ToImVec4(palette.Accent);
		const ImVec4 border = ToImVec4(palette.Border);

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = background;
		colors[ImGuiCol_ChildBg] = background;
		colors[ImGuiCol_PopupBg] = surface;
		colors[ImGuiCol_Border] = border;
		colors[ImGuiCol_Text] = ToImVec4(palette.Text);
		colors[ImGuiCol_TextDisabled] = ToImVec4(palette.TextDisabled);

		colors[ImGuiCol_FrameBg] = surface;
		colors[ImGuiCol_FrameBgHovered] = surfaceHovered;
		colors[ImGuiCol_FrameBgActive] = surfaceActive;

		colors[ImGuiCol_TitleBg] = surface;
		colors[ImGuiCol_TitleBgActive] = surface;
		colors[ImGuiCol_TitleBgCollapsed] = surface;
		colors[ImGuiCol_MenuBarBg] = surface;

		colors[ImGuiCol_Header] = surfaceHovered;
		colors[ImGuiCol_HeaderHovered] = surfaceActive;
		colors[ImGuiCol_HeaderActive] = accent;

		colors[ImGuiCol_Button] = surfaceHovered;
		colors[ImGuiCol_ButtonHovered] = surfaceActive;
		colors[ImGuiCol_ButtonActive] = accent;

		colors[ImGuiCol_Tab] = surface;
		colors[ImGuiCol_TabHovered] = surfaceActive;
		colors[ImGuiCol_TabSelected] = background;
		colors[ImGuiCol_TabDimmed] = surface;
		colors[ImGuiCol_TabDimmedSelected] = background;

		colors[ImGuiCol_Separator] = border;
		colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, kDockingPreviewAlpha);
		colors[ImGuiCol_DockingEmptyBg] = background;
		colors[ImGuiCol_CheckMark] = accent;
		colors[ImGuiCol_SliderGrab] = accent;
		colors[ImGuiCol_ResizeGrip] = surfaceActive;
		colors[ImGuiCol_ResizeGripHovered] = accent;
	}
}
