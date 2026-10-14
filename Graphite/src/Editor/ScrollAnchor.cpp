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

		// WARNING: Both row numbers are widened before the subtraction. Subtracting one
		// std::size_t from another turns the anchor moving up into an enormous move down.
		const double rowsMoved =
			static_cast<double>(screenRowNow) - static_cast<double>(anchor.ScreenRow);

		return std::max(0.0f, anchor.ScrollY + static_cast<float>(rowsMoved * lineHeight));
	}

	ScrollAnchor AnchorAtLine(std::size_t line)
	{
		ScrollAnchor anchor;
		anchor.Line = line;
		anchor.Taken = true;
		return anchor;
	}

	bool PendingTopLine::StillWanted(std::size_t caretOffset, bool pendingScroll,
									 bool userScrolled) const
	{
		return caretOffset == CaretOffset && !pendingScroll && !userScrolled;
	}

	bool PendingTopLine::Settle(const ScrollAnchor& drawnAnchor)
	{
		if (drawnAnchor.Taken && drawnAnchor.Line == Line)
		{
			return true;
		}

		--FramesLeft;
		return FramesLeft <= 0;
	}
}
