#include "Editor/Hover/HoverLayout.h"

#include "Core/Base/Ascii.h"
#include "Core/Text/Columns.h"

#include "Language/Registry/LanguageRegistry.h"

#include <algorithm>
#include <cmath>

namespace Graphite {

	DedentedPreview DedentPreview(const std::vector<std::string>& lines, std::size_t tabWidth)
	{
		DedentedPreview dedented;
		std::vector<std::string>& expandedLines = dedented.Lines;
		expandedLines.reserve(lines.size());

		std::size_t sharedIndent = std::string::npos;

		for (const std::string& line : lines)
		{
			expandedLines.push_back(Core::ExpandTabs(line, tabWidth));

			const std::size_t leadingSpaces = expandedLines.back().find_first_not_of(' ');

			if (leadingSpaces != std::string::npos)
			{
				sharedIndent = std::min(sharedIndent, leadingSpaces);
			}
		}

		if (sharedIndent == std::string::npos)
		{
			return dedented;
		}

		for (std::string& line : expandedLines)
		{
			if (Core::ContainsOnlySpacesAndTabs(line))
			{
				line.clear();
			}
			else
			{
				line.erase(0, sharedIndent);
			}
		}

		dedented.Removed = sharedIndent;

		return dedented;
	}

	std::vector<SemanticSpan> DedentSpans(const std::vector<SemanticSpan>& spans,
										  std::size_t removedColumns)
	{
		std::vector<SemanticSpan> movedSpans;
		movedSpans.reserve(spans.size());

		for (const SemanticSpan& span : spans)
		{
			if (span.From < removedColumns)
			{
				continue;
			}

			movedSpans.push_back({span.From - removedColumns, span.To - removedColumns, span.Kind});
		}

		return movedSpans;
	}

	float PreviewWidth(std::size_t widestColumns, float cellWidth, bool besideScrollbar)
	{
		constexpr std::size_t kScrollbarClearanceColumns = 1;
		const std::size_t clearanceColumns = besideScrollbar ? kScrollbarClearanceColumns : 0;
		const std::size_t columns = widestColumns + clearanceColumns;

		// WARNING: Rounded up because ImGui truncates a window's size, and a window a fraction
		// narrower than its content grows a horizontal scrollbar over it.
		return std::ceil(static_cast<float>(columns) * cellWidth);
	}

	TooltipPlacement PlaceTooltip(ImVec2 anchorMin, ImVec2 anchorMax, ImVec2 size,
								  ImVec2 boundsMin, ImVec2 boundsMax)
	{
		constexpr ImVec2 kHangBelowPivot(0.0f, 0.0f);
		constexpr ImVec2 kStandAbovePivot(0.0f, 1.0f);

		const float roomBelow = std::max(0.0f, boundsMax.y - anchorMax.y);
		const float roomAbove = std::max(0.0f, anchorMin.y - boundsMin.y);
		const bool fitsBelow = size.y <= roomBelow;
		const bool fitsAbove = size.y <= roomAbove;
		const bool goesAbove = !fitsBelow && (fitsAbove || roomAbove > roomBelow);

		TooltipPlacement placement;
		placement.Position.x = std::max(boundsMin.x, std::min(anchorMin.x, boundsMax.x - size.x));
		placement.Position.y = goesAbove ? anchorMin.y : anchorMax.y;
		placement.Pivot = goesAbove ? kStandAbovePivot : kHangBelowPivot;
		placement.MaxHeight = goesAbove ? roomAbove : roomBelow;

		return placement;
	}

	bool NeedsDeclarationSpans(LanguageId language)
	{
		return DefinitionOf(language).Syntax == SyntaxKind::CppLexer;
	}
}
