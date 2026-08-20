#pragma once

#include <cstdint>

namespace Graphite {

	// Everything the application draws around the editor: panels, tabs, popups, the window
	// border. Eight, because eight is what Core::Theme's own table already distinguishes —
	// this is that table's vocabulary, not a new one.
	enum class ChromeColor : std::uint8_t
	{
		Background,
		Surface,
		SurfaceHovered,
		SurfaceActive,
		Accent,
		Text,
		TextDisabled,

		// Both ImGui's borders and separators, and the border DWM paints around the window.
		Border,
		Count
	};
}
