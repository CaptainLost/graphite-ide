#pragma once

#include "Editor/Breadcrumb.h"
#include "Editor/StatusBar/StatusBar.h"
#include "Hex/HexNavigation.h"
#include "Theme/EditorPalette.h"

#include "Core/Events/FrameInput.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace Graphite {

	class HexFile;

	struct HexViewState
	{
		HexSelection Selection;
		HexScroll Scroll;
		std::optional<std::uint64_t> Hovered;
		std::uint64_t SeenChange = 0;
	};

	struct HexViewInput
	{
		// Both must be set.
		const BreadcrumbModel* Path = nullptr;
		const EditorPalette* Palette = nullptr;

		// The editor's code font size, before the interface scale.
		float FontSize = 0.0f;

		// What the router left of this frame's strokes; only the moves are taken, and only while the view holds the
		// keyboard.
		const Core::FrameInput* Keys = nullptr;

		// One line above the header, such as why an image is shown as bytes; empty for none.
		std::string_view Notice;
	};

	struct HexViewFrame
	{
		std::vector<ViewStatusItem> Status;

		// Whether the tab's window holds the keyboard focus, a child of it included.
		bool Focused = false;
	};

	// One hex tab's content in the window being drawn: the path row, the notice, a header fixed above the rows, the rows
	// on screen with a scrollbar of their own, or the reason a read failed with Retry. The owner updates `file` once a
	// frame before this is drawn.
	HexViewFrame DrawHexView(HexFile& file, HexViewState& state, const HexViewInput& input);
}
