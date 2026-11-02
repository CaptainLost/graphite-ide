#pragma once

#include "Core/Base/NonCopyable.h"

#include <imgui.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	class TabCloseRequests;

	inline constexpr float kDockTabBarHeight = 32.0f;

	// What stands before a tab's name when the tab's owner draws it, such as a file kind's badge: how wide it is, and
	// how to draw it given the top-left corner of the name's line.
	struct DockTabLead
	{
		float Width = 0.0f;
		std::function<void(ImVec2 lineTopLeft)> Draw;
	};

	struct DockTabLook
	{
		std::string Name;

		// Drawn before the name, in the name's colour, when the tab has no lead.
		std::string Glyph;

		std::optional<DockTabLead> Lead;

		// The face the name is drawn in; none draws it in the window's own.
		ImFont* Face = nullptr;

		// A dot after the name, in this colour.
		std::optional<ImU32> Dot;
	};

	// The look a window's name gives its tab: a Lucide glyph at the name's start, then the words after the spaces
	// that follow it. What follows a "##" is the window's id and is not shown.
	DockTabLook DockTabLookOf(std::string_view windowName);

	// How wide a tab with this look is: its padding, its lead, its name, its dot and its close button.
	float DockTabWidth(const DockTabLook& look);

	// The frame padding above and below the interface font that makes a dock tab bar 32 pixels tall.
	float DockTabBarFramePaddingY();

	// Where a tab bar's first tab starts: at the node's own edge, in a dock space, or inside the border of the window a
	// floating node draws round itself.
	enum class DockTabBarEdge
	{
		AtTheNode,
		InsideTheWindowBorder
	};

	// The style dock tab bars are laid out in while it lives: tab against tab from the bar's start, as tall as
	// kDockTabBarHeight, with borderless overflow arrows and without ImGui's own close button, which DrawDockTab draws
	// in its place. At the node it is the dock space's, and the padding and shrink floor it applies are the ones the
	// floating nodes' bars are laid out with on the frames after.
	class DockTabBarStyle final : NonCopyable
	{
	public:
		explicit DockTabBarStyle(DockTabBarEdge edge = DockTabBarEdge::AtTheNode);

		// Laid out with this frame padding above and below the font rather than DockTabBarFramePaddingY's.
		DockTabBarStyle(DockTabBarEdge edge, float framePaddingY);

		// Laid out with this padding, and shrinking a crowded bar's tabs no narrower than this.
		DockTabBarStyle(DockTabBarEdge edge, float framePaddingY, float narrowestShrunkenTab);

		~DockTabBarStyle();

	private:
		ImVec2 m_FramePadding;
		float m_ItemInnerSpacingX = 0.0f;
		float m_FrameBorderSize = 0.0f;
		float m_WindowBorderSize = 0.0f;
		float m_CloseButtonMinWidthSelected = 0.0f;
		float m_CloseButtonMinWidthUnselected = 0.0f;
		float m_TabMinWidthShrink = 0.0f;
	};

	// Lays out the tab bars of floating dock nodes, which ImGui updates in NewFrame, in DockTabBarStyle too, at the
	// padding the dock space's applied on the frame before: a context hook applies it for NewFrame's span. Once per
	// context; DockSpaceHost calls it.
	void KeepFloatingTabBarsFlat();

	// Draws a line along the bottom of every tab bar the dock space and the floating dock nodes showed this frame.
	// DrawDockTab paints each tab over it and draws it again under every tab but the selected one.
	void DrawDockTabBarLines(ImGuiID dockSpaceId);

	// Begins a window that may be docked with the frame padding its node's tab bar is laid out with, so the window's
	// content starts below the bar.
	bool BeginDockedWindow(const char* name, bool* open, ImGuiWindowFlags flags);

	// Draws the current window's tab with this look, over the tab ImGui drew, and closes the window through the close
	// requests when its close button or a middle click asks to. Called after the window's Begin.
	void DrawDockTab(const DockTabLook& look, TabCloseRequests& closeRequests);
}
