#include "Persistence/CaretRestore.h"

#include "Core/Text/Caret.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>

namespace Graphite {

	std::size_t RestoredCaretOffset(const Core::TextBuffer& buffer, std::size_t line, std::size_t byteColumn)
	{
		const std::size_t clampedOffset = buffer.ToOffset(Core::Position{line, byteColumn});

		return Core::CharacterBoundaryAtOrBefore(buffer, clampedOffset);
	}

	std::size_t RestoredTopLine(const Core::TextBuffer& buffer, std::size_t savedTopLine)
	{
		const std::size_t lineCount = buffer.GetLineCount();
		const bool isEmpty = lineCount == 0;

		if (isEmpty)
		{
			return 0;
		}

		const std::size_t lastLine = lineCount - 1;

		return std::min(savedTopLine, lastLine);
	}
}
