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

		constexpr Color kBorder{0.141f, 0.153f, 0.173f, 1.0f};
		constexpr Color kContent{0.106f, 0.114f, 0.129f, 1.0f};

		constexpr Color FromImVec4(const ImVec4& color)
		{
			return Color{color.x, color.y, color.z, color.w};
		}

		constexpr float kLightBackgroundLuminance = 0.5f;
		constexpr float kRedLuminanceWeight = 0.2126f;
		constexpr float kGreenLuminanceWeight = 0.7152f;
		constexpr float kBlueLuminanceWeight = 0.0722f;
		constexpr float kEmphasisShareTowardTheExtreme = 0.6f;
		constexpr float kDockingPreviewAlpha = 0.45f;
		constexpr float kScrollbarGrabAlpha = 0.12f;
		constexpr float kScrollbarGrabHoveredAlpha = 0.20f;
		constexpr float kScrollbarGrabActiveAlpha = 0.28f;
		constexpr float kScrollbarSize = 12.0f;
		constexpr float kScrollbarGrabInset = 4.0f;
		constexpr float kSquareTabs = 0.0f;
		constexpr float kIndentSpacing = 15.0f;
		constexpr float kSelectableRounding = 4.0f;
		constexpr float kDockSplitterWidth = 1.0f;
		constexpr float kNoOverline = 0.0f;
		constexpr float kNoTabBarLine = 0.0f;
		constexpr float kTransparent = 0.0f;

		constexpr ImVec4 WithAlpha(const ImVec4& colour, float alpha)
		{
			return ImVec4(colour.x, colour.y, colour.z, alpha);
		}

		constexpr ImVec4 Lerp(const ImVec4& from, const ImVec4& to, float share)
		{
			return ImVec4(from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share,
						  from.z + (to.z - from.z) * share, from.w + (to.w - from.w) * share);
		}
	}

	bool IsLightBackground(const Color& background)
	{
		const float luminance = kRedLuminanceWeight * background.R + kGreenLuminanceWeight * background.G +
								kBlueLuminanceWeight * background.B;

		return luminance > kLightBackgroundLuminance;
	}

	ImU32 EmphasisText()
	{
		const ImVec4* colours = ImGui::GetStyle().Colors;
		const ImVec4& text = colours[ImGuiCol_Text];
		const float extremeChannel = IsLightBackground(FromImVec4(colours[ImGuiCol_WindowBg])) ? 0.0f : 1.0f;
		const ImVec4 extreme(extremeChannel, extremeChannel, extremeChannel, text.w);

		return ImGui::GetColorU32(Lerp(text, extreme, kEmphasisShareTowardTheExtreme));
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
		style.TabRounding = kSquareTabs;
		style.GrabRounding = 3.0f;
		// WARNING: MenuItem and BeginMenu swap MenuItemRounding in for SelectableRounding while they draw, so a menu's highlight is rounded only if MenuItemRounding is set too.
		style.SelectableRounding = kSelectableRounding;
		style.MenuItemRounding = kSelectableRounding;
		style.WindowBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;
		style.WindowPadding = ImVec2(8.0f, 8.0f);
		style.FramePadding = ImVec2(8.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 5.0f);
		style.IndentSpacing = kIndentSpacing;
		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.ScrollbarSize = kScrollbarSize;
		style.ScrollbarPadding = kScrollbarGrabInset;
		style.TabBarOverlineSize = kNoOverline;
		style.TabBarBorderSize = kNoTabBarLine;
		style.DockingNodeHasCloseButton = false;
		style.DockingSeparatorSize = kDockSplitterWidth;
	}

	void Theme::Apply(const ChromePalette& palette)
	{
		// WARNING: about twenty-five ImGuiCol_ entries are not set below and keep the base style's colours, so the base must match the palette's luminance.
		if (IsLightBackground(palette.Background))
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

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = background;
		colors[ImGuiCol_ChildBg] = background;
		colors[ImGuiCol_PopupBg] = surface;
		// WARNING: ImGui draws dock splitters in Border, not Separator: it pushes Separator = Border around them. Border is therefore the line colour, and a popup is told apart by its fill.
		colors[ImGuiCol_Border] = border;
		colors[ImGuiCol_Text] = text;
		colors[ImGuiCol_TextDisabled] = ToImVec4(palette.TextDisabled);

		// There is no pressed colour: a press keeps the hover fill, and the accent is never a fill.
		colors[ImGuiCol_FrameBg] = surface;
		colors[ImGuiCol_FrameBgHovered] = surfaceHovered;
		colors[ImGuiCol_FrameBgActive] = surfaceHovered;

		colors[ImGuiCol_TitleBg] = background;
		colors[ImGuiCol_TitleBgActive] = background;
		colors[ImGuiCol_TitleBgCollapsed] = background;
		colors[ImGuiCol_MenuBarBg] = background;

		// WARNING: a menu fills its open item, and a combo its current item, in Header, so Header keeps a fill.
		colors[ImGuiCol_Header] = surfaceHovered;
		colors[ImGuiCol_HeaderHovered] = surfaceHovered;
		colors[ImGuiCol_HeaderActive] = surfaceHovered;

		colors[ImGuiCol_Button] = surfaceHovered;
		colors[ImGuiCol_ButtonHovered] = surfaceHovered;
		colors[ImGuiCol_ButtonActive] = surfaceHovered;

		colors[ImGuiCol_Tab] = background;
		colors[ImGuiCol_TabHovered] = background;
		colors[ImGuiCol_TabSelected] = background;
		colors[ImGuiCol_TabDimmed] = background;
		colors[ImGuiCol_TabDimmedSelected] = background;

		colors[ImGuiCol_ScrollbarBg] = WithAlpha(background, kTransparent);
		colors[ImGuiCol_ScrollbarGrab] = WithAlpha(text, kScrollbarGrabAlpha);
		colors[ImGuiCol_ScrollbarGrabHovered] = WithAlpha(text, kScrollbarGrabHoveredAlpha);
		colors[ImGuiCol_ScrollbarGrabActive] = WithAlpha(text, kScrollbarGrabActiveAlpha);

		colors[ImGuiCol_Separator] = border;
		colors[ImGuiCol_TableHeaderBg] = surface;
		colors[ImGuiCol_TableBorderStrong] = border;
		colors[ImGuiCol_TableBorderLight] = border;
		colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, kDockingPreviewAlpha);
		colors[ImGuiCol_DockingEmptyBg] = ToImVec4(palette.Content);
		colors[ImGuiCol_CheckMark] = accent;
		colors[ImGuiCol_SliderGrab] = accent;
		colors[ImGuiCol_ResizeGrip] = surfaceActive;
		colors[ImGuiCol_ResizeGripHovered] = accent;
		colors[ImGuiCol_ResizeGripActive] = accent;
		colors[ImGuiCol_NavCursor] = accent;
	}
}
