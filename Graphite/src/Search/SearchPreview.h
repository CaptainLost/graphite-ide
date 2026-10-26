#pragma once

#include "Core/Text/Columns.h"
#include "Search/SearchQuery.h"
#include "Theme/ColorScheme.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Graphite {

	class PreviewContent;

	enum class PreviewRequest : std::uint8_t
	{
		None,
		OpenFile
	};

	struct SearchPreviewInput
	{
		const std::vector<SearchHit>& Hits;

		std::size_t Current = 0;

		const ResolvedScheme& Scheme;

		std::size_t TabWidth = Core::DefaultTabWidth;

		float FontSize = 0.0f;
	};

	// Asks to open the file when its preview offers Open in place of text it would not read.
	PreviewRequest DrawSearchPreview(PreviewContent& content, const SearchPreviewInput& input);
}
