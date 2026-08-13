#include "corepch.h"

#include "Core/Text/Caret.h"

#include "Core/Text/Columns.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>

namespace Core {

	namespace {
		enum class CharacterClass
		{
			Letter,
			Whitespace,
			Punctuation
		};

		CharacterClass ClassOf(char value)
		{
			const unsigned char byte = static_cast<unsigned char>(value);

			// Anything outside ASCII counts as a letter, which makes `variableName_ąę` one
			// word rather than three.
			if (byte >= 0x80)
				return CharacterClass::Letter;

			if (byte == ' ' || byte == '\t' || byte == '\n')
				return CharacterClass::Whitespace;

			const bool alphanumeric = (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte == '_';

			return alphanumeric ? CharacterClass::Letter : CharacterClass::Punctuation;
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

		std::size_t VerticalTarget(const TextBuffer& buffer, const Caret& caret,
								   std::size_t lines, bool up, std::size_t tabWidth,
								   std::size_t column)
		{
			const Position position = buffer.ToPosition(caret.Offset);

			const std::size_t line =
				up ? (position.Line > lines ? position.Line - lines : 0)
				   : std::min(position.Line + lines, buffer.GetLineCount() - 1);

			return OffsetOfColumn(buffer, line, column, tabWidth);
		}

		std::size_t WordBoundaryForward(const TextBuffer& buffer, std::size_t offset)
		{
			if (offset >= buffer.GetLength())
				return buffer.GetLength();

			const CharacterClass start = ClassOf(buffer.CharAt(offset));
			std::size_t at = offset;

			while (at < buffer.GetLength() && ClassOf(buffer.CharAt(at)) == start)
				++at;

			return at;
		}

		std::size_t WordBoundaryBackward(const TextBuffer& buffer, std::size_t offset)
		{
			if (offset == 0)
				return 0;

			const CharacterClass start = ClassOf(buffer.CharAt(offset - 1));
			std::size_t at = offset;

			while (at > 0 && ClassOf(buffer.CharAt(at - 1)) == start)
				--at;

			return at;
		}

		bool IsContinuation(const TextBuffer& buffer, std::size_t offset)
		{
			return (static_cast<unsigned char>(buffer.CharAt(offset)) & 0xC0) == 0x80;
		}

		std::size_t NextCharacterIn(const TextBuffer& buffer, std::size_t offset)
		{
			if (offset >= buffer.GetLength())
				return buffer.GetLength();

			std::size_t next = offset + 1;

			while (next < buffer.GetLength() && IsContinuation(buffer, next))
				++next;

			return next;
		}

		std::size_t PreviousCharacterIn(const TextBuffer& buffer, std::size_t offset)
		{
			if (offset == 0 || buffer.IsEmpty())
				return 0;

			std::size_t previous = std::min(offset, buffer.GetLength()) - 1;

			while (previous > 0 && IsContinuation(buffer, previous))
				--previous;

			return previous;
		}

		std::size_t SnapToBoundary(const TextBuffer& buffer, std::size_t offset)
		{
			std::size_t at = std::min(offset, buffer.GetLength());

			while (at > 0 && at < buffer.GetLength() && IsContinuation(buffer, at))
				--at;

			return at;
		}

		Caret Settle(Caret caret, std::size_t offset, bool extend)
		{
			caret.Offset = offset;

			if (!extend)
				caret.Anchor = offset;

			return caret;
		}
	}

	bool HasSelection(const Caret& caret)
	{
		return caret.Offset != caret.Anchor;
	}

	std::pair<std::size_t, std::size_t> SelectionRange(const Caret& caret)
	{
		return caret.Offset < caret.Anchor ? std::pair{caret.Offset, caret.Anchor}
										   : std::pair{caret.Anchor, caret.Offset};
	}

	Caret Move(const TextBuffer& buffer, Caret caret, Motion motion, bool extend,
			   std::size_t tabWidth, std::size_t pageLines)
	{
		const Position position = buffer.ToPosition(caret.Offset);

		std::size_t target = caret.Offset;
		bool vertical = false;
		std::size_t verticalColumn = 0;

		switch (motion)
		{
		case Motion::Left:
			target = PreviousCharacterIn(buffer, caret.Offset);
			break;
		case Motion::Right:
			target = NextCharacterIn(buffer, caret.Offset);
			break;
		case Motion::Up:
			verticalColumn = DesiredColumnFor(buffer, caret, tabWidth);
			target = VerticalTarget(buffer, caret, 1, true, tabWidth, verticalColumn);
			vertical = true;
			break;
		case Motion::Down:
			verticalColumn = DesiredColumnFor(buffer, caret, tabWidth);
			target = VerticalTarget(buffer, caret, 1, false, tabWidth, verticalColumn);
			vertical = true;
			break;
		case Motion::PageUp:
			verticalColumn = DesiredColumnFor(buffer, caret, tabWidth);
			target = VerticalTarget(buffer, caret, pageLines, true, tabWidth, verticalColumn);
			vertical = true;
			break;
		case Motion::PageDown:
			verticalColumn = DesiredColumnFor(buffer, caret, tabWidth);
			target = VerticalTarget(buffer, caret, pageLines, false, tabWidth, verticalColumn);
			vertical = true;
			break;
		case Motion::LineStart:
			target = buffer.LineStart(position.Line);
			break;
		case Motion::LineEnd:
			target = buffer.LineStart(position.Line) + buffer.LineLength(position.Line);
			break;
		case Motion::DocumentStart:
			target = 0;
			break;
		case Motion::DocumentEnd:
			target = buffer.GetLength();
			break;
		case Motion::WordLeft:
			target = WordBoundaryBackward(buffer, caret.Offset);
			break;
		case Motion::WordRight:
			target = WordBoundaryForward(buffer, caret.Offset);
			break;
		}

		Caret result = Settle(caret, target, extend);

		// Vertical movement carries the column it aimed at forward, even when this line was
		// too short to reach it; every other motion re-aims from where the caret now sits.
		result.DesiredColumn = vertical ? verticalColumn : ColumnAt(buffer, result.Offset, tabWidth);

		return result;
	}

	Caret PlaceAt(const TextBuffer& buffer, Caret caret, std::size_t offset, bool extend,
				  std::size_t tabWidth)
	{
		Caret result = Settle(caret, SnapToBoundary(buffer, offset), extend);
		result.DesiredColumn = ColumnAt(buffer, result.Offset, tabWidth);

		return result;
	}

	Caret SelectWordAt(const TextBuffer& buffer, std::size_t offset)
	{
		const std::size_t at = SnapToBoundary(buffer, offset);

		Caret caret;
		caret.Anchor = WordBoundaryBackward(buffer, at == buffer.GetLength() ? at : at + 1);
		caret.Offset = WordBoundaryForward(buffer, at);
		caret.DesiredColumn = NoDesiredColumn;

		return caret;
	}

	Caret SelectLineAt(const TextBuffer& buffer, std::size_t line)
	{
		Caret caret;
		caret.Anchor = buffer.LineStart(line);
		caret.Offset = line + 1 < buffer.GetLineCount() ? buffer.LineStart(line + 1)
														: buffer.GetLength();
		caret.DesiredColumn = NoDesiredColumn;

		return caret;
	}

	Caret SelectAll(const TextBuffer& buffer)
	{
		Caret caret;
		caret.Anchor = 0;
		caret.Offset = buffer.GetLength();
		caret.DesiredColumn = NoDesiredColumn;

		return caret;
	}
}