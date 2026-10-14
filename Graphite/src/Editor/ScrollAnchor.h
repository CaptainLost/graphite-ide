#pragma once

#include <cstddef>
#include <optional>

namespace Graphite {
	struct ScrollAnchor
	{
		std::size_t Line = 0;

		std::size_t SubRow = 0;

		std::size_t ScreenRow = 0;

		float ScrollY = 0.0f;

		bool Taken = false;
	};

	std::size_t AnchoredRow(const ScrollAnchor& anchor, std::size_t firstRowOfLine,
							std::size_t rowsOfLine);

	std::optional<float> ScrollKeepingAnchor(const ScrollAnchor& anchor, std::size_t screenRowNow,
											 float lineHeight);

	ScrollAnchor AnchorAtLine(std::size_t line);

	struct PendingTopLine
	{
		// WARNING: One frame is not always enough, because ImGui clamps a scroll to the content size the
		// previous frame measured, and a document's first frame measured none.
		static constexpr int kFrames = 30;

		std::size_t Line = 0;

		std::size_t CaretOffset = 0;

		int FramesLeft = kFrames;

		bool StillWanted(std::size_t caretOffset, bool pendingScroll, bool userScrolled) const;

		bool Settle(const ScrollAnchor& drawnAnchor);
	};
}
