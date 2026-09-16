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
		// The document row the top of the viewport sat on.
		std::size_t DocumentRow = 0;

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
}
