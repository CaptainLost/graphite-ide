#pragma once

#include "Core/Text/Columns.h"
#include "Search/SearchQuery.h"
#include "Theme/ColorScheme.h"

#include <cstddef>
#include <vector>

namespace Graphite {

	class PreviewContent;

	struct SearchPreviewInput
	{
		const std::vector<SearchHit>& Hits;

		std::size_t Current = 0;

		const ResolvedScheme& Scheme;

		std::size_t TabWidth = Core::DefaultTabWidth;

		float FontSize = 0.0f;
	};

	void DrawSearchPreview(PreviewContent& content, const SearchPreviewInput& input);
}
