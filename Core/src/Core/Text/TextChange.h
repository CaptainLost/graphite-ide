#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Core {

	// How many changes a buffer keeps for readers catching up. A reader further behind than
	// this recomputes whatever it derived from the text.
	inline constexpr std::size_t kChangeLogCapacity = 1024;

	struct TextChange
	{
		std::uint64_t Sequence = 0;

		std::size_t Offset = 0;
		std::size_t RemovedLength = 0;
		std::size_t InsertedLength = 0;

		// The line the change starts on, the newlines it removed and inserted, and the buffer's
		// line count right after it.
		std::size_t FirstLine = 0;
		std::size_t RemovedLines = 0;
		std::size_t InsertedLines = 0;
		std::size_t LineCountAfter = 1;

		// A reload: no offset in the old text stands for anything in the new one.
		bool WholeContent = false;
	};

	struct ChangedLines
	{
		std::size_t FirstLine = 0;

		// In the text as it is after the last change.
		std::size_t LastLine = 0;

		bool Whole = false;
	};

	// Measured from both ends: lines above the first change and below the last are untouched
	// whatever order the changes came in. The span must not be empty.
	ChangedLines LinesChangedBy(std::span<const TextChange> changes);

	// Text inserted at the offset pushes it on; an offset inside removed text lands where the
	// removal started.
	std::size_t ShiftOffset(std::size_t offset, std::span<const TextChange> changes);

	class TextBuffer;

	// The first line whose text the changes moved to another line. A change with text of its
	// line before it leaves that line where it was: a newline typed at the end of a declaration
	// moves the lines below, not the declaration. Asks the buffer after the changes; the span
	// must not be empty.
	std::size_t FirstMovedLine(const TextBuffer& buffer, std::span<const TextChange> changes);
}
