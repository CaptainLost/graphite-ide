#include "Editor/HoverLayout.h"

#include "Core/Text/Columns.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	std::vector<std::string> DedentPreviewLines(const std::vector<std::string>& lines,
												std::size_t tabWidth)
	{
		std::vector<std::string> expanded;
		expanded.reserve(lines.size());

		// After expansion leading whitespace is spaces alone, so a count of them is a column.
		std::size_t shared = std::string::npos;

		for (const std::string& line : lines)
		{
			expanded.push_back(Core::ExpandTabs(line, tabWidth));

			const std::size_t indent = expanded.back().find_first_not_of(' ');

			if (indent != std::string::npos)
				shared = std::min(shared, indent);
		}

		if (shared == std::string::npos)
			return expanded;

		for (std::string& line : expanded)
		{
			if (line.find_first_not_of(' ') == std::string::npos)
				line.clear();
			else
				line.erase(0, shared);
		}

		return expanded;
	}

	PreviewBox PreviewBoxSize(std::size_t widestColumns, std::size_t rows,
							  const PreviewMetrics& metrics)
	{
		const bool tall = rows > metrics.MaxRows;

		// Rounded up: ImGui truncates a window's size, and a child a fraction narrower than its
		// content grows a horizontal scrollbar over its only row.
		const float content = std::ceil(static_cast<float>(widestColumns) * metrics.CellWidth);

		// Glyphs drawn flush against the vertical scrollbar read as cut off, even when every
		// pixel of them is there.
		const float wanted =
			content + (tall ? metrics.CellWidth + metrics.ScrollbarSize : 0.0f);
		const bool wide = wanted > metrics.MaxWidth;

		PreviewBox box;
		box.Width = std::min(wanted, metrics.MaxWidth);
		box.Height = static_cast<float>(std::min(rows, metrics.MaxRows)) * metrics.LineHeight +
					 (wide ? metrics.ScrollbarSize : 0.0f);

		return box;
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
