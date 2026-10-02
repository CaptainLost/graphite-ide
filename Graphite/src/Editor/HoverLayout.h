#pragma once

#include <imgui.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Graphite {

	// The preview's lines as the tooltip draws them: tabs expanded, then the indentation every
	// non-blank line shares taken off. A method previewed from the middle of a class would
	// otherwise open on a run of empty cells, and pay for them in width. Blank lines do not count
	// toward what is shared, and lose whatever whitespace they had; a preview with no non-blank
	// line is returned expanded and otherwise as it was.
	std::vector<std::string> DedentPreviewLines(const std::vector<std::string>& lines,
												std::size_t tabWidth);

	struct PreviewMetrics
	{
		float CellWidth = 0.0f;
		float LineHeight = 0.0f;
		float ScrollbarSize = 0.0f;
		float MaxWidth = 0.0f;       // the widest the preview's child may be
		std::size_t MaxRows = 0;     // the tallest, in rows; a longer preview scrolls
	};

	struct PreviewBox
	{
		float Width = 0.0f;
		float Height = 0.0f;
	};

	// The size of the preview's scrolling child for a widest line of `widestColumns` cells and
	// `rows` lines. As wide as that line, up to the cap; a tall preview adds its vertical
	// scrollbar and a cell clear of it, and a wide one the room its horizontal scrollbar takes.
	PreviewBox PreviewBoxSize(std::size_t widestColumns, std::size_t rows,
							  const PreviewMetrics& metrics);

	struct TooltipPlacement
	{
		ImVec2 Position{};
		ImVec2 Pivot{};          // (0, 0) hangs the tooltip below Position, (0, 1) stands it above
		float MaxHeight = 0.0f;  // the room on the side it went to; a taller tooltip scrolls
	};

	// Where a tooltip of `size` goes for a token at [anchorMin, anchorMax], kept within
	// [boundsMin, boundsMax] — the editor surface's visible rectangle, so it never spills over the
	// panels docked around the editor. Below the token if it fits there, above it if it fits
	// there instead, else on whichever side has more room, held to that room. Flush against the
	// token's row either way: the shorter the path from the token to the tooltip, the less of it
	// the mouse has to cross.
	TooltipPlacement PlaceTooltip(ImVec2 anchorMin, ImVec2 anchorMax, ImVec2 size,
								  ImVec2 boundsMin, ImVec2 boundsMax);
}
