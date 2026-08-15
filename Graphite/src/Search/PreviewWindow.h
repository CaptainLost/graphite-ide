#pragma once

#include <cstddef>

namespace Graphite {

	// About twenty lines, and an odd number of them so the hit's line sits in the middle with
	// as much above it as below.
	inline constexpr std::size_t kPreviewSpan = 21;

	// An inclusive range of line indices. Inclusive rather than half-open because every caller
	// draws it with `for (line = First; line <= Last; ++line)`, and a half-open end would be
	// one past a line that may be the last one in the buffer.
	struct PreviewWindow
	{
		std::size_t First = 0;
		std::size_t Last = 0;

		friend bool operator==(const PreviewWindow&, const PreviewWindow&) = default;
	};

	// The window to draw around `line` in a buffer of `lineCount` lines.
	//
	// Clamped at both ends and never shorter than it has to be: a hit near the top shows the
	// span from line 0 rather than centring on a negative line, a hit near the end shows the
	// last `span` lines rather than running past them, and a file shorter than the span shows
	// all of it. A `line` past the end is clamped rather than refused — the line number comes
	// from a search that ran some time ago, and the file may have shrunk since.
	//
	// `lineCount` of zero answers {0, 0}; Core::TextBuffer always reports at least one line,
	// so this is defence rather than a case a caller can reach.
	PreviewWindow WindowAround(std::size_t line, std::size_t lineCount, std::size_t span);
}
