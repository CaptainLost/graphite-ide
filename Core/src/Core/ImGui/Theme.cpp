#include "corepch.h"

#include "Core/ImGui/Theme.h"

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

		constexpr ImVec4 ToImVec4(const Color& color)
		{
			return ImVec4{color.r, color.g, color.b, color.a};
		}
	}

	Color Theme::GetBorderColor()
	{
		return kBorder;
	}

	void Theme::ApplyDark()
	{
		ImGui::StyleColorsDark();

		ImGuiStyle& style = ImGui::GetStyle();

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

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_WindowBg] = kBackground;
		colors[ImGuiCol_ChildBg] = kBackground;
		colors[ImGuiCol_PopupBg] = kSurface;
		colors[ImGuiCol_Border] = ToImVec4(kBorder);
		colors[ImGuiCol_Text] = kText;
		colors[ImGuiCol_TextDisabled] = kTextDisabled;

		colors[ImGuiCol_FrameBg] = kSurface;
		colors[ImGuiCol_FrameBgHovered] = kSurfaceHovered;
		colors[ImGuiCol_FrameBgActive] = kSurfaceActive;

		colors[ImGuiCol_TitleBg] = kSurface;
		colors[ImGuiCol_TitleBgActive] = kSurface;
		colors[ImGuiCol_TitleBgCollapsed] = kSurface;
		colors[ImGuiCol_MenuBarBg] = kSurface;

		colors[ImGuiCol_Header] = kSurfaceHovered;
		colors[ImGuiCol_HeaderHovered] = kSurfaceActive;
		colors[ImGuiCol_HeaderActive] = kAccent;

		colors[ImGuiCol_Button] = kSurfaceHovered;
		colors[ImGuiCol_ButtonHovered] = kSurfaceActive;
		colors[ImGuiCol_ButtonActive] = kAccent;

		colors[ImGuiCol_Tab] = kSurface;
		colors[ImGuiCol_TabHovered] = kSurfaceActive;
		colors[ImGuiCol_TabSelected] = kBackground;
		colors[ImGuiCol_TabDimmed] = kSurface;
		colors[ImGuiCol_TabDimmedSelected] = kBackground;

		colors[ImGuiCol_Separator] = ToImVec4(kBorder);
		colors[ImGuiCol_DockingPreview] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.45f);
		colors[ImGuiCol_DockingEmptyBg] = kBackground;
		colors[ImGuiCol_CheckMark] = kAccent;
		colors[ImGuiCol_SliderGrab] = kAccent;
		colors[ImGuiCol_ResizeGrip] = kSurfaceActive;
		colors[ImGuiCol_ResizeGripHovered] = kAccent;
	}
}