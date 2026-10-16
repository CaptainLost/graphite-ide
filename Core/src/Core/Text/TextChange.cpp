#include "corepch.h"

#include "Core/Text/TextChange.h"

#include "Core/Base/Ascii.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <limits>
#include <string>

namespace Core {

	ChangedLines LinesChangedBy(std::span<const TextChange> changes)
	{
		ChangedLines lines;

		if (changes.empty())
		{
			return lines;
		}

		std::size_t firstLine = changes.front().FirstLine;
		std::size_t untouchedLinesAtEnd = std::numeric_limits<std::size_t>::max();

		for (const TextChange& change : changes)
		{
			lines.Whole = lines.Whole || change.WholeContent;
			firstLine = std::min(firstLine, change.FirstLine);

			const std::size_t lastTouchedLine = change.FirstLine + change.InsertedLines;
			untouchedLinesAtEnd = std::min(untouchedLinesAtEnd, change.LineCountAfter - 1 - lastTouchedLine);
		}

		lines.FirstLine = firstLine;
		lines.LastLine = changes.back().LineCountAfter - 1 - untouchedLinesAtEnd;

		return lines;
	}

	std::size_t ShiftOffset(std::size_t offset, std::span<const TextChange> changes)
	{
		for (const TextChange& change : changes)
		{
			if (change.WholeContent)
			{
				offset = std::min(offset, change.InsertedLength);
				continue;
			}

			const std::size_t removedEnd = change.Offset + change.RemovedLength;

			if (offset >= removedEnd)
			{
				offset = offset - change.RemovedLength + change.InsertedLength;
			}
			else if (offset > change.Offset)
			{
				offset = change.Offset;
			}
		}

		return offset;
	}

	std::size_t FirstMovedLine(const TextBuffer& buffer, std::span<const TextChange> changes)
	{
		std::size_t firstMovedLine = std::numeric_limits<std::size_t>::max();

		for (const TextChange& change : changes)
		{
			// The text before the change on its first line is the same before and after it, so
			// the buffer as it is now can say whether that line kept anything of its own.
			const std::size_t lineStart = buffer.LineStart(change.FirstLine);
			const std::string textBeforeChange = buffer.Substring(lineStart, change.Offset);
			const bool lineKeepsItsText = !ContainsOnlySpacesAndTabs(textBeforeChange);

			firstMovedLine = std::min(firstMovedLine, change.FirstLine + (lineKeepsItsText ? 1 : 0));
		}

		return firstMovedLine;
	}
}
