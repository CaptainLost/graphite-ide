#include "Editor/ScrollAnchor.h"

#include <algorithm>

namespace Graphite {

	std::size_t AnchoredRow(const ScrollAnchor& anchor, std::size_t firstRowOfLine,
							std::size_t rowsOfLine)
	{
		if (rowsOfLine == 0)
		{
			return firstRowOfLine;
		}

		return firstRowOfLine + std::min(anchor.SubRow, rowsOfLine - 1);
	}

	std::optional<float> ScrollKeepingAnchor(const ScrollAnchor& anchor, std::size_t screenRowNow,
											 float lineHeight)
	{
		if (lineHeight <= 0.0f || screenRowNow == anchor.ScreenRow)
		{
			return std::nullopt;
		}

		// Both row numbers widened before either is subtracted. Taking one std::size_t from
		// another would turn the anchor moving up — which is as ordinary as it moving down, and
		// happens whenever rows above it go away — into an enormous move in the other direction.
		const double moved =
			static_cast<double>(screenRowNow) - static_cast<double>(anchor.ScreenRow);

		return std::max(0.0f, anchor.ScrollY + static_cast<float>(moved * lineHeight));
	}
}
