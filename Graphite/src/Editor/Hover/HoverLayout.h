#pragma once

#include "Editor/Overlays/SemanticOverlay.h"
#include "Language/Api/LanguageId.h"

#include <imgui.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct DedentedPreview
	{
		std::vector<std::string> Lines;
		std::size_t Removed = 0;
	};

	DedentedPreview DedentPreview(const std::vector<std::string>& lines, std::size_t tabWidth);

	std::vector<SemanticSpan> DedentSpans(const std::vector<SemanticSpan>& spans,
										  std::size_t removedColumns);

	bool NeedsDeclarationSpans(LanguageId language);

	float PreviewWidth(std::size_t widestColumns, float cellWidth, bool besideScrollbar);

	struct TooltipPlacement
	{
		ImVec2 Position{};
		ImVec2 Pivot{};
		float MaxHeight = 0.0f;
	};

	TooltipPlacement PlaceTooltip(ImVec2 anchorMin, ImVec2 anchorMax, ImVec2 size,
								  ImVec2 boundsMin, ImVec2 boundsMax);
}
