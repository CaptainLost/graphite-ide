#include "Persistence/CaretRestore.h"

#include "Core/Text/TextBuffer.h"

#include <algorithm>

namespace Graphite {

	namespace {
		bool IsContinuationByte(char byte)
		{
			return (static_cast<unsigned char>(byte) & 0xC0u) == 0x80u;
		}
	}

	std::size_t RestoredCaretOffset(const Core::TextBuffer& buffer, std::size_t line,
									std::size_t column)
	{
		// ToOffset already clamps both the line and the column.
		std::size_t offset = buffer.ToOffset(Core::Position{line, column});

		while (offset > 0 && offset < buffer.GetLength() && IsContinuationByte(buffer.CharAt(offset)))
		{
			--offset;
		}

		return offset;
	}

	std::size_t RestoredTopLine(const Core::TextBuffer& buffer, std::size_t top)
	{
		const std::size_t lines = buffer.GetLineCount();

		return lines == 0 ? 0 : std::min(top, lines - 1);
	}
}
