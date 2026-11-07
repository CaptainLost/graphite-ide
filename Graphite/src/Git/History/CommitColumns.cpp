#include "Git/History/CommitColumns.h"

#include <algorithm>

namespace Graphite {

	namespace {

		constexpr float kMaxGraphShareOfTable = 0.25f;
		constexpr float kMockupFontPixels = 13.0f;
		constexpr float kHashFromMockupPixels = 500.0f;
		constexpr float kAuthorNameLeastCharacters = 3.0f;
	}

	CommitColumns LayoutCommitColumns(const CommitColumnsInput& input)
	{
		const float pixelsPerMockupPixel = input.FontSize / kMockupFontPixels;

		CommitColumns columns;
		columns.Graph = std::min(input.Graph, input.Available * kMaxGraphShareOfTable);
		columns.ShowHash = input.Available >= kHashFromMockupPixels * pixelsPerMockupPixel;

		return columns;
	}

	bool ShowsAuthorName(float columnWidth, float avatarAndGap, float characterWidth)
	{
		return columnWidth >= avatarAndGap + kAuthorNameLeastCharacters * characterWidth;
	}
}
