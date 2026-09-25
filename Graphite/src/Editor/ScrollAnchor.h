#pragma once

#include <cstddef>
#include <optional>

namespace Graphite {

	// Where a document's viewport was, in terms of the document rather than of pixels.
	//
	// ImGui scrolls in pixels, and the screen row a document row draws at does not stay put: a
	// lens row arriving above the viewport pushes everything below it down by a row, and so does
	// rewrapping into a narrower window. Nothing else in the editor moves the scroll except
	// following the caret, so without this the pixel offset holds still while the content slides
	// underneath it — and the reader, who did nothing, is somewhere else in the file. A server's
	// reply landing three seconds after a file opens used to move the view by as many rows as
	// there were declarations above it.
	struct ScrollAnchor
	{
		// The document LINE the top of the viewport sat on, and how far into its wrapped rows.
		//
		// A line, not a row index, and that distinction is the whole of whether rewrapping is
		// covered. A row index names a position in a mapping that rewrapping rebuilds, so after a
		// narrower window recomputed the breaks the same index names different text and the
		// correction would either not fire or fire in the wrong direction. A line names the same
		// text however it is broken up.
		std::size_t Line = 0;

		// Which of that line's rows was at the top. Kept because a long wrapped line can fill the
		// viewport by itself, and anchoring to its first row would jump the reader to the start of
		// the paragraph they were in the middle of.
		std::size_t SubRow = 0;

		// The screen row that drew it at the time. This is what the correction compares against:
		// the same document row drawing at a different screen row is precisely the event this
		// exists to notice, and it catches a row inserted above just as well as one removed.
		std::size_t ScreenRow = 0;

		// The scroll that went with the two above, remainder included — the viewport rarely sits
		// on a row boundary, and carrying the remainder is what makes the correction invisible
		// rather than merely close.
		float ScrollY = 0.0f;

		// False until a frame has actually drawn something to anchor to. A document opened and
		// not yet drawn has no place to be kept in.
		bool Taken = false;
	};

	// The document row the anchor names now, given what its line has become.
	//
	// `firstRowOfLine` and `rowsOfLine` are that line's RowMap answers this frame. Separated from
	// the map itself so the one part with a decision in it — what to do when the line no longer
	// has the row that was recorded — is testable without building a document: rewrapping into a
	// wider window leaves a line shorter than the sub-row taken from it, and the reader then ends
	// at the last row of the line they were inside, which is the nearest place still there.
	//
	// A line with no rows answers with the line's own first row rather than one before it.
	std::size_t AnchoredRow(const ScrollAnchor& anchor, std::size_t firstRowOfLine,
							std::size_t rowsOfLine);

	// The scroll that puts the anchored document row back where it was, now that it draws at
	// `screenRowNow`.
	//
	// Empty when there is nothing to write back — the row has not moved, or there is no row
	// height to measure a move in. A caller therefore never writes a scroll equal to the one it
	// already has, which on a frame somebody is dragging the scrollbar would be a fight.
	//
	// Clamped at the top of the document: more rows can vanish from above the viewport than there
	// was scroll to give back, and a negative offset is not a position.
	std::optional<float> ScrollKeepingAnchor(const ScrollAnchor& anchor, std::size_t screenRowNow,
											 float lineHeight);

	// An anchor asking for `line` at the top of the viewport. ScrollKeepingAnchor reads it as
	// that line having drawn at screen row zero with nothing scrolled, so the first frame that
	// draws the document scrolls to wherever the line draws now, lens rows included.
	ScrollAnchor AnchorAtLine(std::size_t line);

	// A top line restored from the last launch, held until a frame has actually shown it. One
	// frame is not always enough: ImGui clamps a scroll to the content size the previous frame
	// measured, and a document's first frame measured none. And not held forever: a line near
	// the end of the file can never reach the top.
	struct PendingTopLine
	{
		static constexpr int kFrames = 30;

		std::size_t Line = 0;
		int FramesLeft = kFrames;

		// Called after each frame that drew the document, with the anchor that frame took.
		// True once there is nothing left to do.
		bool Settle(const ScrollAnchor& drawn);
	};
}
