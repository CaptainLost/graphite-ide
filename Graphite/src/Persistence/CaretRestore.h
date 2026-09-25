#pragma once

#include <cstddef>

namespace Core {

	class TextBuffer;

}

namespace Graphite {

	// Where a caret saved as a line and a byte column lands in the document as it is now. The
	// file may have changed since it was saved: a line past the end lands at the end, a column
	// past the line's end at the line's end, and a column inside a UTF-8 sequence at the start
	// of that sequence.
	std::size_t RestoredCaretOffset(const Core::TextBuffer& buffer, std::size_t line,
									std::size_t column);

	// A saved top line, no further down than the document's last line.
	std::size_t RestoredTopLine(const Core::TextBuffer& buffer, std::size_t top);
}
