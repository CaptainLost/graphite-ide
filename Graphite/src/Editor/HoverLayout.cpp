#include "Editor/HoverLayout.h"

#include "Core/Text/Columns.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	DedentedPreview DedentPreview(const std::vector<std::string>& lines, std::size_t tabWidth)
	{
		DedentedPreview dedented;
		std::vector<std::string>& expanded = dedented.Lines;
		expanded.reserve(lines.size());

		// After expansion leading whitespace is spaces alone, so a count of them is a column.
		std::size_t shared = std::string::npos;

		for (const std::string& line : lines)
		{
			expanded.push_back(Core::ExpandTabs(line, tabWidth));

			const std::size_t indent = expanded.back().find_first_not_of(' ');

			if (indent != std::string::npos)
			{
				shared = std::min(shared, indent);
			}
		}

		if (shared == std::string::npos)
		{
			return dedented;
		}

		for (std::string& line : expanded)
		{
			if (line.find_first_not_of(' ') == std::string::npos)
			{
				line.clear();
			}
			else
			{
				line.erase(0, shared);
			}
		}

		dedented.Removed = shared;

		return dedented;
	}

	std::vector<SemanticSpan> DedentSpans(const std::vector<SemanticSpan>& spans,
										  std::size_t removed)
	{
		std::vector<SemanticSpan> moved;
		moved.reserve(spans.size());

		for (const SemanticSpan& span : spans)
		{
			if (span.From < removed)
			{
				continue;
			}

			moved.push_back({span.From - removed, span.To - removed, span.Kind});
		}

		return moved;
	}

	float PreviewWidth(std::size_t widestColumns, float cellWidth, bool besideScrollbar)
	{
		const std::size_t columns = widestColumns + (besideScrollbar ? 1 : 0);

		// Rounded up: ImGui truncates a window's size, and a window a fraction narrower than its
		// content grows a horizontal scrollbar over it.
		return std::ceil(static_cast<float>(columns) * cellWidth);
	}

	TooltipPlacement PlaceTooltip(ImVec2 anchorMin, ImVec2 anchorMax, ImVec2 size,
								  ImVec2 boundsMin, ImVec2 boundsMax)
	{
		const float below = std::max(0.0f, boundsMax.y - anchorMax.y);
		const float above = std::max(0.0f, anchorMin.y - boundsMin.y);
		const bool fitsBelow = size.y <= below;
		const bool fitsAbove = size.y <= above;
		const bool flip = !fitsBelow && (fitsAbove || above > below);

		TooltipPlacement placed;
		placed.Position.x = std::max(boundsMin.x, std::min(anchorMin.x, boundsMax.x - size.x));
		placed.Position.y = flip ? anchorMin.y : anchorMax.y;
		placed.Pivot = flip ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
		placed.MaxHeight = flip ? above : below;

		return placed;
	}
}
