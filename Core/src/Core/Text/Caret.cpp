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

			// Anything outside ASCII is a letter: it makes `variableName_ąę` one word,
			// which is what a programmer means by a word here.
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

			return ColumnOfByte(buffer.LineText(position.line), position.column, tabWidth);
		}

		std::size_t OffsetOfColumn(const TextBuffer& buffer, std::size_t line,
								   std::size_t column, std::size_t tabWidth)
		{
			return buffer.LineStart(line) + ByteOfColumn(buffer.LineText(line), column, tabWidth);
		}

		std::size_t DesiredColumnFor(const TextBuffer& buffer, const Caret& caret,
									 std::size_t tabWidth)
		{
			return caret.desiredColumn == NoDesiredColumn
					   ? ColumnAt(buffer, caret.offset, tabWidth)
					   : caret.desiredColumn;
		}

		std::size_t VerticalTarget(const TextBuffer& buffer, const Caret& caret,
								   std::size_t lines, bool up, std::size_t tabWidth,
								   std::size_t column)
		{
			const Position position = buffer.ToPosition(caret.offset);

			const std::size_t line =
				up ? (position.line > lines ? position.line - lines : 0)
				   : std::min(position.line + lines, buffer.GetLineCount() - 1);

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
			caret.offset = offset;

			if (!extend)
				caret.anchor = offset;

			return caret;
		}
	}

	bool HasSelection(const Caret& caret)
	{
		return caret.offset != caret.anchor;
	}

	std::pair<std::size_t, std::size_t> SelectionRange(const Caret& caret)
	{
		return caret.offset < caret.anchor ? std::pair{caret.offset, caret.anchor}
										   : std::pair{caret.anchor, caret.offset};
	}

	Caret Move(const TextBuffer& buffer, Caret caret, Motion motion, bool extend,
			   std::size_t tabWidth, std::size_t pageLines)
	{
		const Position position = buffer.ToPosition(caret.offset);

		std::size_t target = caret.offset;
		bool vertical = false;
		std::size_t verticalColumn = 0;

		switch (motion)
		{
		case Motion::Left:
			target = PreviousCharacterIn(buffer, caret.offset);
			break;
		case Motion::Right:
			target = NextCharacterIn(buffer, caret.offset);
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
			target = buffer.LineStart(position.line);
			break;
		case Motion::LineEnd:
			target = buffer.LineStart(position.line) + buffer.LineLength(position.line);
			break;
		case Motion::DocumentStart:
			target = 0;
			break;
		case Motion::DocumentEnd:
			target = buffer.GetLength();
			break;
		case Motion::WordLeft:
			target = WordBoundaryBackward(buffer, caret.offset);
			break;
		case Motion::WordRight:
			target = WordBoundaryForward(buffer, caret.offset);
			break;
		}

		Caret result = Settle(caret, target, extend);

		// Vertical movement keeps aiming at the column it started with — even when this
		// line is too short to reach it — so it carries the resolved column forward
		// rather than the (possibly shorter) column it actually landed on. Every other
		// motion redefines where the caret is aiming from the position it now sits at.
		result.desiredColumn = vertical ? verticalColumn : ColumnAt(buffer, result.offset, tabWidth);

		return result;
	}

	Caret PlaceAt(const TextBuffer& buffer, Caret caret, std::size_t offset, bool extend,
				  std::size_t tabWidth)
	{
		Caret result = Settle(caret, SnapToBoundary(buffer, offset), extend);
		result.desiredColumn = ColumnAt(buffer, result.offset, tabWidth);

		return result;
	}

	Caret SelectWordAt(const TextBuffer& buffer, std::size_t offset)
	{
		const std::size_t at = SnapToBoundary(buffer, offset);

		Caret caret;
		caret.anchor = WordBoundaryBackward(buffer, at == buffer.GetLength() ? at : at + 1);
		caret.offset = WordBoundaryForward(buffer, at);
		caret.desiredColumn = NoDesiredColumn;

		return caret;
	}

	Caret SelectLineAt(const TextBuffer& buffer, std::size_t line)
	{
		Caret caret;
		caret.anchor = buffer.LineStart(line);
		caret.offset = line + 1 < buffer.GetLineCount() ? buffer.LineStart(line + 1)
														: buffer.GetLength();
		caret.desiredColumn = NoDesiredColumn;

		return caret;
	}

	Caret SelectAll(const TextBuffer& buffer)
	{
		Caret caret;
		caret.anchor = 0;
		caret.offset = buffer.GetLength();
		caret.desiredColumn = NoDesiredColumn;

		return caret;
	}
}