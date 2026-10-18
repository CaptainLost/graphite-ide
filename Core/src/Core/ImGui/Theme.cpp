#include "corepch.h"

#include "Core/ImGui/Theme.h"

#include "Core/ImGui/ImGuiColors.h"
#include <imgui.h>

namespace Core {

	namespace {
		constexpr ImVec4 kBackground{0.078f, 0.082f, 0.094f, 1.0f};
		constexpr ImVec4 kSurface{0.145f, 0.157f, 0.176f, 1.0f};
		constexpr ImVec4 kSurfaceHovered{0.196f, 0.212f, 0.239f, 1.0f};
		constexpr ImVec4 kSurfaceActive{0.243f, 0.263f, 0.298f, 1.0f};
		constexpr ImVec4 kAccent{0.259f, 0.522f, 0.957f, 1.0f};
		constexpr ImVec4 kText{0.839f, 0.859f, 0.886f, 1.0f};
		constexpr ImVec4 kTextDisabled{0.482f, 0.510f, 0.553f, 1.0f};

		constexpr Color kBorder{0.149f, 0.161f, 0.180f, 1.0f};
		constexpr Color kContent{0.106f, 0.114f, 0.129f, 1.0f};

		constexpr Color FromImVec4(const ImVec4& color)
		{
			return Color{color.x, color.y, color.z, color.w};
		}

		constexpr float kLightBackgroundLuminance = 0.5f;
		constexpr float kRedLuminanceWeight = 0.2126f;
		constexpr float kGreenLuminanceWeight = 0.7152f;
		constexpr float kBlueLuminanceWeight = 0.0722f;
		constexpr float kDockingPreviewAlpha = 0.45f;
		constexpr float kTabBarShade = 0.03f;
		constexpr float kScrollbarGrabAlpha = 0.12f;
		constexpr float kScrollbarGrabHoveredAlpha = 0.20f;
		constexpr float kScrollbarGrabActiveAlpha = 0.28f;
		constexpr float kScrollbarSize = 12.0f;
		constexpr float kNoOverline = 0.0f;
		constexpr float kNoTabBarLine = 0.0f;
		constexpr float kTransparent = 0.0f;

		constexpr float ClampAtZero(float value)
		{
			return value > 0.0f ? value : 0.0f;
		}

		constexpr ImVec4 ShadeBelow(const ImVec4& colour, float amount)
		{
			return ImVec4(ClampAtZero(colour.x - amount), ClampAtZero(colour.y - amount), ClampAtZero(colour.z - amount), colour.w);
		}

		constexpr ImVec4 WithAlpha(const ImVec4& colour, float alpha)
		{
			return ImVec4(colour.x, colour.y, colour.z, alpha);
		}

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
		palette.Content = kContent;

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
		style.ScrollbarSize = kScrollbarSize;
		style.TabBarOverlineSize = kNoOverline;
		style.TabBarBorderSize = kNoTabBarLine;
		style.DockingNodeHasCloseButton = false;
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
		const ImVec4 text = ToImVec4(palette.Text);
		const ImVec4 tabBar = ShadeBelow(background, kTabBarShade);

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = background;
		colors[ImGuiCol_ChildBg] = background;
		colors[ImGuiCol_PopupBg] = surface;
		colors[ImGuiCol_Border] = surfaceActive;
		colors[ImGuiCol_Text] = text;
		colors[ImGuiCol_TextDisabled] = ToImVec4(palette.TextDisabled);

		colors[ImGuiCol_FrameBg] = surface;
		colors[ImGuiCol_FrameBgHovered] = surfaceHovered;
		colors[ImGuiCol_FrameBgActive] = surfaceActive;

		colors[ImGuiCol_TitleBg] = tabBar;
		colors[ImGuiCol_TitleBgActive] = tabBar;
		colors[ImGuiCol_TitleBgCollapsed] = tabBar;
		colors[ImGuiCol_MenuBarBg] = background;

		colors[ImGuiCol_Header] = surfaceHovered;
		colors[ImGuiCol_HeaderHovered] = surfaceActive;
		colors[ImGuiCol_HeaderActive] = accent;

		colors[ImGuiCol_Button] = surfaceHovered;
		colors[ImGuiCol_ButtonHovered] = surfaceActive;
		colors[ImGuiCol_ButtonActive] = accent;

		colors[ImGuiCol_Tab] = tabBar;
		colors[ImGuiCol_TabHovered] = background;
		colors[ImGuiCol_TabSelected] = background;
		colors[ImGuiCol_TabDimmed] = tabBar;
		colors[ImGuiCol_TabDimmedSelected] = background;

		colors[ImGuiCol_ScrollbarBg] = WithAlpha(background, kTransparent);
		colors[ImGuiCol_ScrollbarGrab] = WithAlpha(text, kScrollbarGrabAlpha);
		colors[ImGuiCol_ScrollbarGrabHovered] = WithAlpha(text, kScrollbarGrabHoveredAlpha);
		colors[ImGuiCol_ScrollbarGrabActive] = WithAlpha(text, kScrollbarGrabActiveAlpha);

		colors[ImGuiCol_Separator] = border;
		colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, kDockingPreviewAlpha);
		colors[ImGuiCol_DockingEmptyBg] = ToImVec4(palette.Content);
		colors[ImGuiCol_CheckMark] = accent;
		colors[ImGuiCol_SliderGrab] = accent;
		colors[ImGuiCol_ResizeGrip] = surfaceActive;
		colors[ImGuiCol_ResizeGripHovered] = accent;
	}
}
