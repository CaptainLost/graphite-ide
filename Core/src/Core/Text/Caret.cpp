#include "corepch.h"

#include "Core/Text/Caret.h"

#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/WordBounds.h"

#include <algorithm>

namespace Core {

	namespace {
		constexpr std::size_t kOneLine = 1;

		enum class CharacterClass
		{
			Word,
			Whitespace,
			Punctuation
		};

		enum class VerticalDirection
		{
			Up,
			Down
		};

		CharacterClass ClassOf(char character)
		{
			if (IsWordByte(character))
			{
				return CharacterClass::Word;
			}

			const bool isWhitespace = character == ' ' || character == '\t' || character == '\n';
			if (isWhitespace)
			{
				return CharacterClass::Whitespace;
			}

			return CharacterClass::Punctuation;
		}

		std::size_t ColumnAt(const TextBuffer& buffer, std::size_t offset,
							 std::size_t tabWidth)
		{
			const Position position = buffer.ToPosition(offset);

			return ColumnOfByte(buffer.LineText(position.Line), position.Column, tabWidth);
		}

		std::size_t OffsetOfColumn(const TextBuffer& buffer, std::size_t line,
								   std::size_t column, std::size_t tabWidth)
		{
			return buffer.LineStart(line) + ByteOfColumn(buffer.LineText(line), column, tabWidth);
		}

		std::size_t DesiredColumnFor(const TextBuffer& buffer, const Caret& caret,
									 std::size_t tabWidth)
		{
			return caret.DesiredColumn == NoDesiredColumn
					   ? ColumnAt(buffer, caret.Offset, tabWidth)
					   : caret.DesiredColumn;
		}

		std::size_t LineAbove(std::size_t line, std::size_t linesToMove)
		{
			if (line > linesToMove)
			{
				return line - linesToMove;
			}

			return 0;
		}

		std::size_t LineBelow(const TextBuffer& buffer, std::size_t line, std::size_t linesToMove)
		{
			const std::size_t lastLine = buffer.GetLineCount() - 1;

			return std::min(line + linesToMove, lastLine);
		}

		std::size_t VerticalTarget(const TextBuffer& buffer, const Caret& caret,
								   std::size_t linesToMove, VerticalDirection direction,
								   std::size_t tabWidth, std::size_t column)
		{
			const Position position = buffer.ToPosition(caret.Offset);

			const std::size_t targetLine = direction == VerticalDirection::Up
											   ? LineAbove(position.Line, linesToMove)
											   : LineBelow(buffer, position.Line, linesToMove);

			return OffsetOfColumn(buffer, targetLine, column, tabWidth);
		}

		std::size_t WordBoundaryForward(const TextBuffer& buffer, std::size_t offset)
		{
			if (offset >= buffer.GetLength())
			{
				return buffer.GetLength();
			}

			const CharacterClass startClass = ClassOf(buffer.CharAt(offset));
			std::size_t boundary = offset;

			while (boundary < buffer.GetLength() && ClassOf(buffer.CharAt(boundary)) == startClass)
			{
				++boundary;
			}

			return boundary;
		}

		std::size_t WordBoundaryBackward(const TextBuffer& buffer, std::size_t offset)
		{
			if (offset == 0)
			{
				return 0;
			}

			const CharacterClass startClass = ClassOf(buffer.CharAt(offset - 1));
			std::size_t boundary = offset;

			while (boundary > 0 && ClassOf(buffer.CharAt(boundary - 1)) == startClass)
			{
				--boundary;
			}

			return boundary;
		}

		std::size_t NextCharacterIn(const TextBuffer& buffer, std::size_t offset)
		{
			if (offset >= buffer.GetLength())
			{
				return buffer.GetLength();
			}

			std::size_t next = offset + 1;

			while (next < buffer.GetLength() && IsUtf8ContinuationByte(buffer.CharAt(next)))
			{
				++next;
			}

			return next;
		}

		std::size_t PreviousCharacterIn(const TextBuffer& buffer, std::size_t offset)
		{
			if (offset == 0 || buffer.IsEmpty())
			{
				return 0;
			}

			std::size_t previous = std::min(offset, buffer.GetLength()) - 1;

			while (previous > 0 && IsUtf8ContinuationByte(buffer.CharAt(previous)))
			{
				--previous;
			}

			return previous;
		}

		bool IsInsideCharacter(const TextBuffer& buffer, std::size_t offset)
		{
			const bool isBetweenTheEnds = offset > 0 && offset < buffer.GetLength();

			return isBetweenTheEnds && IsUtf8ContinuationByte(buffer.CharAt(offset));
		}

		Caret MovedTo(Caret caret, std::size_t offset, SelectionChange selectionChange)
		{
			caret.Offset = offset;

			if (selectionChange == SelectionChange::Collapse)
			{
				caret.Anchor = offset;
			}

			return caret;
		}
	}

	bool HasSelection(const Caret& caret)
	{
		return caret.Offset != caret.Anchor;
	}

	OffsetRange SelectionRange(const Caret& caret)
	{
		return caret.Offset < caret.Anchor ? OffsetRange{.From = caret.Offset, .To = caret.Anchor}
										   : OffsetRange{.From = caret.Anchor, .To = caret.Offset};
	}

	Caret Move(const TextBuffer& buffer, Caret caret, Motion motion, SelectionChange selectionChange,
			   std::size_t tabWidth, std::size_t pageLines)
	{
		const Position position = buffer.ToPosition(caret.Offset);

		std::size_t targetOffset = caret.Offset;
		bool isVerticalMotion = false;
		std::size_t desiredColumn = 0;

		switch (motion)
		{
		case Motion::Left:
			targetOffset = PreviousCharacterIn(buffer, caret.Offset);
			break;
		case Motion::Right:
			targetOffset = NextCharacterIn(buffer, caret.Offset);
			break;
		case Motion::Up:
			desiredColumn = DesiredColumnFor(buffer, caret, tabWidth);
			targetOffset = VerticalTarget(buffer, caret, kOneLine, VerticalDirection::Up, tabWidth, desiredColumn);
			isVerticalMotion = true;
			break;
		case Motion::Down:
			desiredColumn = DesiredColumnFor(buffer, caret, tabWidth);
			targetOffset = VerticalTarget(buffer, caret, kOneLine, VerticalDirection::Down, tabWidth, desiredColumn);
			isVerticalMotion = true;
			break;
		case Motion::PageUp:
			desiredColumn = DesiredColumnFor(buffer, caret, tabWidth);
			targetOffset = VerticalTarget(buffer, caret, pageLines, VerticalDirection::Up, tabWidth, desiredColumn);
			isVerticalMotion = true;
			break;
		case Motion::PageDown:
			desiredColumn = DesiredColumnFor(buffer, caret, tabWidth);
			targetOffset = VerticalTarget(buffer, caret, pageLines, VerticalDirection::Down, tabWidth, desiredColumn);
			isVerticalMotion = true;
			break;
		case Motion::LineStart:
			targetOffset = buffer.LineStart(position.Line);
			break;
		case Motion::LineEnd:
			targetOffset = buffer.LineStart(position.Line) + buffer.LineLength(position.Line);
			break;
		case Motion::DocumentStart:
			targetOffset = 0;
			break;
		case Motion::DocumentEnd:
			targetOffset = buffer.GetLength();
			break;
		case Motion::WordLeft:
			targetOffset = WordBoundaryBackward(buffer, caret.Offset);
			break;
		case Motion::WordRight:
			targetOffset = WordBoundaryForward(buffer, caret.Offset);
			break;
		}

		Caret moved = MovedTo(caret, targetOffset, selectionChange);
		moved.DesiredColumn = isVerticalMotion ? desiredColumn : ColumnAt(buffer, moved.Offset, tabWidth);

		return moved;
	}

	Caret PlaceAt(const TextBuffer& buffer, Caret caret, std::size_t offset, SelectionChange selectionChange,
				  std::size_t tabWidth)
	{
		const std::size_t boundary = CharacterBoundaryAtOrBefore(buffer, offset);

		Caret placed = MovedTo(caret, boundary, selectionChange);
		placed.DesiredColumn = ColumnAt(buffer, placed.Offset, tabWidth);

		return placed;
	}

	std::size_t CharacterBoundaryAtOrBefore(const TextBuffer& buffer, std::size_t offset)
	{
		std::size_t boundary = std::min(offset, buffer.GetLength());

		while (IsInsideCharacter(buffer, boundary))
		{
			--boundary;
		}

		return boundary;
	}

	Caret SelectWordAt(const TextBuffer& buffer, std::size_t offset)
	{
		const std::size_t boundary = CharacterBoundaryAtOrBefore(buffer, offset);

		const bool isAtEndOfBuffer = boundary == buffer.GetLength();
		const std::size_t backwardSearchStart = isAtEndOfBuffer ? boundary : boundary + 1;

		const std::size_t wordStart = WordBoundaryBackward(buffer, backwardSearchStart);
		const std::size_t wordEnd = WordBoundaryForward(buffer, boundary);

		return Caret{.Offset = wordEnd, .Anchor = wordStart, .DesiredColumn = NoDesiredColumn};
	}

	Caret SelectLineAt(const TextBuffer& buffer, std::size_t line)
	{
		const std::size_t lineStart = buffer.LineStart(line);

		const std::size_t nextLine = line + 1;
		const bool hasNextLine = nextLine < buffer.GetLineCount();
		const std::size_t endIncludingLineEnding = hasNextLine ? buffer.LineStart(nextLine)
															   : buffer.GetLength();

		return Caret{.Offset = endIncludingLineEnding, .Anchor = lineStart, .DesiredColumn = NoDesiredColumn};
	}

	Caret SelectAll(const TextBuffer& buffer)
	{
		const std::size_t documentStart = 0;

		return Caret{.Offset = buffer.GetLength(), .Anchor = documentStart, .DesiredColumn = NoDesiredColumn};
	}
}
