#include "Git/History/CommitColumns.h"

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr float kMaxGraphShareOfTable = 0.25f;
		constexpr float kMockupFontPixels = 13.0f;
		constexpr float kAuthorNameFromMockupPixels = 640.0f;
		constexpr float kHashFromMockupPixels = 500.0f;
	}

	CommitColumns LayoutCommitColumns(const CommitColumnsInput& input)
	{
		const float pixelsPerMockupPixel = input.FontSize / kMockupFontPixels;

		CommitColumns columns;
		columns.Graph = std::min(input.Graph, input.Available * kMaxGraphShareOfTable);
		columns.ShowAuthorName = input.Available >= kAuthorNameFromMockupPixels * pixelsPerMockupPixel;
		columns.ShowHash = input.Available >= kHashFromMockupPixels * pixelsPerMockupPixel;

		return columns;
	}
}
