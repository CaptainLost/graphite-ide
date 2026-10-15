#include "corepch.h"

#include "Core/Text/TextChange.h"

#include <algorithm>
#include <limits>

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
}
