#pragma once

#include "Core/Command/MenuBar.h"
#include "Core/ImGui/CaptionLayout.h"
#include "Core/Platform/Caption.h"

#include <imgui.h>

#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	class Window;

}

namespace Graphite {

	inline constexpr float kCaptionHeight = 28.0f;
	inline constexpr float kMenuItemHeight = 22.0f;
	inline constexpr float kWindowButtonWidth = 42.0f;
	inline constexpr float kWindowButtonGlyphSize = 14.0f;

	inline constexpr ImU32 kCloseHoveredFill = IM_COL32(196, 43, 28, 255);

	struct TitleContext
	{
		std::optional<std::string> Folder;
	};

	enum class WindowButton
	{
		None,
		Minimize,
		Maximize,
		Close
	};

	std::string FolderLabel(const std::filesystem::path& root);

	// The custom title bar's menus: a row the bar's height, and each top-level menu's fill a little shorter.
	Core::MenuBarHeights TitleMenuHeights();

	// Draws the logo at the cursor, centred in the bar's row, and steps past it.
	void DrawTitleLogo();

	Core::CaptionLayout DrawTitle(const TitleContext& context, float menuEnd, float barWidth);

	// The glyphs of the window buttons at rest.
	ImU32 WindowButtonGlyphColour();

	// Reports the button that was pressed; drawn in the current window, which spans the bar.
	WindowButton DrawWindowButtons(const Core::CaptionLayout& layout, Core::WindowState state, bool systemOwnsPointer);

	class TitleBar
	{
	public:
		void RenderLogo(Core::Window& window);

		void Render(Core::Window& window, const TitleContext& context);
	};
}
