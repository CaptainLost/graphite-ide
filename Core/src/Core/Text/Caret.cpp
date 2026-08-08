#include "corepch.h"

#include "Core/Text/Caret.h"

#include "Core/Text/Columns.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/TextBuffer.h"

#include <algorithm>
#include <string>

namespace core::text
{
    namespace
    {
        enum class CharacterClass
        {
            Letter,
            Whitespace,
            Punctuation
        };

        [[nodiscard]] CharacterClass ClassOf(char value)
        {
            const unsigned char byte = static_cast<unsigned char>(value);

            // Anything outside ASCII is a letter: it makes `variableName_ąę` one word,
            // which is what a programmer means by a word here.
            if (byte >= 0x80)
                return CharacterClass::Letter;

            if (byte == ' ' || byte == '\t' || byte == '\n')
                return CharacterClass::Whitespace;

            const bool alphanumeric = (byte >= '0' && byte <= '9')
                                      || (byte >= 'a' && byte <= 'z')
                                      || (byte >= 'A' && byte <= 'Z') || byte == '_';

            return alphanumeric ? CharacterClass::Letter : CharacterClass::Punctuation;
        }

        /// The caret's column, given the whole document.
        [[nodiscard]] std::size_t ColumnAt(const TextBuffer& buffer, std::size_t offset,
                                           std::size_t tabWidth)
        {
            const Position position = buffer.ToPosition(offset);

            return ColumnOfByte(buffer.LineText(position.line), position.column, tabWidth);
        }

        /// The offset of `column` on `line`, clamped to that line's end.
        [[nodiscard]] std::size_t OffsetOfColumn(const TextBuffer& buffer, std::size_t line,
                                                 std::size_t column, std::size_t tabWidth)
        {
            return buffer.LineStart(line) + ByteOfColumn(buffer.LineText(line), column, tabWidth);
        }

        /// The column a vertical motion should aim for: the caret's stored desiredColumn,
        /// or its actual current column if that is further right. The two usually agree —
        /// desiredColumn is kept in sync by every non-vertical motion — but a caret handed
        /// in with a stale or unset desiredColumn (offset 5 on column 5, desiredColumn 0)
        /// must still travel to its real column rather than snapping to column 0.
        [[nodiscard]] std::size_t DesiredColumnFor(const TextBuffer& buffer, const Caret& caret,
                                                   std::size_t tabWidth)
        {
            return std::max(ColumnAt(buffer, caret.offset, tabWidth), caret.desiredColumn);
        }

        [[nodiscard]] std::size_t VerticalTarget(const TextBuffer& buffer, const Caret& caret,
                                                 std::size_t lines, bool up, std::size_t tabWidth,
                                                 std::size_t column)
        {
            const Position position = buffer.ToPosition(caret.offset);

            const std::size_t line =
                up ? (position.line > lines ? position.line - lines : 0)
                   : std::min(position.line + lines, buffer.LineCount() - 1);

            return OffsetOfColumn(buffer, line, column, tabWidth);
        }

        [[nodiscard]] std::size_t WordBoundaryForward(const TextBuffer& buffer, std::size_t offset)
        {
            if (offset >= buffer.Length())
                return buffer.Length();

            const CharacterClass start = ClassOf(buffer.CharAt(offset));
            std::size_t at = offset;

            while (at < buffer.Length() && ClassOf(buffer.CharAt(at)) == start)
                ++at;

            return at;
        }

        [[nodiscard]] std::size_t WordBoundaryBackward(const TextBuffer& buffer, std::size_t offset)
        {
            if (offset == 0)
                return 0;

            const CharacterClass start = ClassOf(buffer.CharAt(offset - 1));
            std::size_t at = offset;

            while (at > 0 && ClassOf(buffer.CharAt(at - 1)) == start)
                --at;

            return at;
        }

        /// Pulls an arbitrary offset back onto a character boundary.
        [[nodiscard]] std::size_t SnapToBoundary(const TextBuffer& buffer, std::size_t offset)
        {
            std::size_t at = std::min(offset, buffer.Length());

            while (at > 0 && at < buffer.Length()
                   && (static_cast<unsigned char>(buffer.CharAt(at)) & 0xC0) == 0x80)
                --at;

            return at;
        }

        [[nodiscard]] Caret Settle(Caret caret, std::size_t offset, bool extend)
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
        const std::string text = buffer.Text();
        const Position position = buffer.ToPosition(caret.offset);

        std::size_t target = caret.offset;
        bool vertical = false;
        std::size_t verticalColumn = 0;

        switch (motion)
        {
        case Motion::Left:
            target = PreviousCharacter(text, caret.offset);
            break;
        case Motion::Right:
            target = NextCharacter(text, caret.offset);
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
            target = buffer.Length();
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
        caret.anchor = WordBoundaryBackward(buffer, at == buffer.Length() ? at : at + 1);
        caret.offset = WordBoundaryForward(buffer, at);
        caret.desiredColumn = 0;

        return caret;
    }

    Caret SelectLineAt(const TextBuffer& buffer, std::size_t line)
    {
        Caret caret;
        caret.anchor = buffer.LineStart(line);
        caret.offset = line + 1 < buffer.LineCount() ? buffer.LineStart(line + 1)
                                                     : buffer.Length();
        caret.desiredColumn = 0;

        return caret;
    }

    Caret SelectAll(const TextBuffer& buffer)
    {
        Caret caret;
        caret.anchor = 0;
        caret.offset = buffer.Length();
        caret.desiredColumn = 0;

        return caret;
    }
}
