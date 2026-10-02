#pragma once

#include "Editor/SemanticOverlay.h"

#include <imgui.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Graphite {

	struct DedentedPreview
	{
		std::vector<std::string> Lines;
		std::size_t Removed = 0; // the columns taken off every non-blank line
	};

	// The preview's lines as the tooltip draws them: tabs expanded, then the indentation every
	// non-blank line shares taken off. A method previewed from the middle of a class would
	// otherwise open on a run of empty cells, and pay for them in width. Blank lines do not count
	// toward what is shared, and lose whatever whitespace they had; a preview with no non-blank
	// line is returned expanded and otherwise as it was.
	DedentedPreview DedentPreview(const std::vector<std::string>& lines, std::size_t tabWidth);

	// A server's spans for one line of the file, moved onto that line as the preview draws it:
	// `removed` columns further left. A span that started inside what was taken off is dropped,
	// since ColourOf matches spans by where they start and nothing starts there any more.
	std::vector<SemanticSpan> DedentSpans(const std::vector<SemanticSpan>& spans,
										  std::size_t removed);

	// How wide the preview's rows are for a widest line of `widestColumns` cells. Beside the
	// tooltip's vertical scrollbar it keeps a cell clear of it: glyphs drawn flush against the
	// scrollbar read as cut off, even when every pixel of them is there.
	float PreviewWidth(std::size_t widestColumns, float cellWidth, bool besideScrollbar);

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
