#pragma once

#include "Core/Text/Columns.h"
#include "Search/SearchQuery.h"
#include "Theme/ColorScheme.h"

#include <cstddef>
#include <vector>

namespace Graphite {

	class PreviewContent;

	// Bundled rather than passed one at a time: two of these are std::size_t and a caller can
	// transpose them with nothing said. Fill the fields by name.
	struct SearchPreviewInput
	{
		// Every hit in the previewed file, so occurrences other than the selected one light up
		// too when they fall inside the window. The panel already groups hits by file, so this
		// is the selected hit's group.
		const std::vector<SearchHit>& Hits;

		// Index into Hits of the selected one. The window is centred on its line and it is the
		// one drawn in the current-match colour.
		std::size_t Current = 0;

		const ResolvedScheme& Scheme;

		std::size_t TabWidth = Core::DefaultTabWidth;

		float FontSize = 0.0f;
	};

	// Draws the window around the selected hit into the current ImGui window. Call inside the
	// child the preview occupies; this does not open one.
	//
	// A window onto twenty lines, not a second editor: no editing, no word wrap, no git gutter,
	// no indent guides.
	void DrawSearchPreview(PreviewContent& content, const SearchPreviewInput& input);
}
