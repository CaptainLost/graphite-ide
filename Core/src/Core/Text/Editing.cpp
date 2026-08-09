#include "corepch.h"

#include "Core/Text/Editing.h"

#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/Indent.h"
#include "Core/Text/TextDocument.h"

namespace core::text
{
    namespace
    {
        [[nodiscard]] bool Editable(const OpenItem& item)
        {
            return item.document && !item.document->IsReadOnly();
        }

        /// LF-only copy of text arriving from outside the editor.
        [[nodiscard]] std::string NormaliseNewlines(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());

            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '\r')
                {
                    // Swallow the LF of a CRLF pair; a lone CR becomes LF on its own.
                    if (index + 1 < text.size() && text[index + 1] == '\n')
                        ++index;

                    result.push_back('\n');
                    continue;
                }

                result.push_back(text[index]);
            }

            return result;
        }

        /// Repairs indentDepths after an edit that left the document with a new line
        /// count and changed lines [firstLine, lastLine].
        ///
        /// The range recomputed is wider than the range edited. Indent.h gives a blank
        /// line the smaller of the nearest non-blank depths above and below it, so
        /// editing one line can change every blank line adjacent to it, and reaching the
        /// non-blank line past that run is what supplies the anchor to resolve them
        /// against. Nothing outside that widened range can have changed.
        void UpdateIndentDepths(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
        {
            const TextBuffer& buffer = item.document->Buffer();

            item.indentDepths.resize(buffer.LineCount(), 0);

            const auto blank = [&buffer](std::size_t line)
            {
                const std::string text = buffer.LineText(line);
                return text.find_first_not_of(" \t") == std::string::npos;
            };

            std::size_t from = std::min(firstLine, buffer.LineCount() - 1);
            while (from > 0 && blank(from))
                --from;

            std::size_t to = std::min(lastLine, buffer.LineCount() - 1);
            while (to + 1 < buffer.LineCount() && blank(to))
                ++to;

            // IndentDepths resolves blank lines against their neighbours, so it is given
            // the whole widened range at once rather than a line at a time. The range
            // overload returns entries indexed from `from`, not from line 0, so writing
            // them back means offsetting by `from` rather than indexing by line number
            // directly.
            const std::vector<std::size_t> depths = IndentDepths(buffer, DefaultTabWidth, from, to);

            for (std::size_t line = from; line <= to; ++line)
                item.indentDepths[line] = depths[line - from];
        }

        /// The cheap maintenance path, for an edit that touched lines
        /// [firstLine, lastLine].
        ///
        /// widestColumn only ever grows here. It can only fall when the widest line was
        /// itself edited or no longer exists, and those two cases are the only ones that
        /// pay for a full walk. RefreshDerived is declared in Editing.h, so calling it
        /// from here before its definition is fine.
        void UpdateDerived(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
        {
            const TextBuffer& buffer = item.document->Buffer();

            if (item.widestLine >= buffer.LineCount() || item.widestLine >= firstLine)
            {
                RefreshDerived(item);
                return;
            }

            for (std::size_t line = firstLine; line <= lastLine && line < buffer.LineCount();
                 ++line)
            {
                const std::size_t width = ColumnCount(buffer.LineText(line), DefaultTabWidth);

                if (width > item.widestColumn)
                {
                    item.widestColumn = width;
                    item.widestLine = line;
                }
            }

            UpdateIndentDepths(item, firstLine, lastLine);
        }

        /// Erases the selection, growing one with `motion` first when there is none.
        bool DeleteRange(OpenItem& item, Motion motion)
        {
            if (!Editable(item))
                return false;

            TextBuffer& buffer = item.document->Buffer();

            // Move rather than NextCharacter/PreviousCharacter: those take a
            // string_view and would need the surrounding bytes cut out and the offsets
            // translated back, while Move already does exactly this over a buffer and
            // its rules — whole UTF-8 characters, whole words — are the ones CaretTests
            // already covers.
            Caret target = HasSelection(item.caret)
                               ? item.caret
                               : Move(buffer, item.caret, motion, true, DefaultTabWidth, 0);

            const auto [from, to] = SelectionRange(target);

            if (from == to)
                return false;

            const std::size_t firstLine = buffer.ToPosition(from).line;

            buffer.BeginTransaction();
            buffer.Erase(from, to);
            buffer.EndTransaction();

            item.caret.offset = from;
            item.caret.anchor = from;
            item.caret.desiredColumn = NoDesiredColumn;

            UpdateDerived(item, firstLine, firstLine);

            return true;
        }
    }

    void RefreshDerived(OpenItem& item)
    {
        const TextBuffer& buffer = item.document->Buffer();

        item.indentDepths = IndentDepths(buffer, DefaultTabWidth);
        item.widestColumn = 0;
        item.widestLine = 0;

        for (std::size_t line = 0; line < buffer.LineCount(); ++line)
        {
            const std::size_t width = ColumnCount(buffer.LineText(line), DefaultTabWidth);

            if (width > item.widestColumn)
            {
                item.widestColumn = width;
                item.widestLine = line;
            }
        }
    }

    bool InsertText(OpenItem& item, std::string_view utf8)
    {
        if (!Editable(item) || utf8.empty())
            return false;

        const std::string text = NormaliseNewlines(utf8);
        TextBuffer& buffer = item.document->Buffer();
        const auto [from, to] = SelectionRange(item.caret);

        const std::size_t firstLine = buffer.ToPosition(from).line;

        buffer.BeginTransaction();

        if (from != to)
            buffer.Erase(from, to);

        buffer.Insert(from, text);
        buffer.EndTransaction();

        item.caret.offset = from + text.size();
        item.caret.anchor = item.caret.offset;
        item.caret.desiredColumn = NoDesiredColumn;

        UpdateDerived(item, firstLine, buffer.ToPosition(item.caret.offset).line);

        return true;
    }

    bool DeleteBackward(OpenItem& item)
    {
        return DeleteRange(item, Motion::Left);
    }

    bool DeleteForward(OpenItem& item)
    {
        return DeleteRange(item, Motion::Right);
    }

    bool DeleteWordBackward(OpenItem& item)
    {
        return DeleteRange(item, Motion::WordLeft);
    }

    bool DeleteWordForward(OpenItem& item)
    {
        return DeleteRange(item, Motion::WordRight);
    }

    bool Undo(OpenItem& item)
    {
        if (!Editable(item) || !item.document->Buffer().Undo())
            return false;

        TextBuffer& buffer = item.document->Buffer();

        buffer.BreakUndoGroup();
        item.caret.offset = std::min(item.caret.offset, buffer.Length());
        item.caret.anchor = item.caret.offset;
        item.caret.desiredColumn = NoDesiredColumn;

        RefreshDerived(item);

        return true;
    }

    bool Redo(OpenItem& item)
    {
        if (!Editable(item) || !item.document->Buffer().Redo())
            return false;

        TextBuffer& buffer = item.document->Buffer();

        buffer.BreakUndoGroup();
        item.caret.offset = std::min(item.caret.offset, buffer.Length());
        item.caret.anchor = item.caret.offset;
        item.caret.desiredColumn = NoDesiredColumn;

        RefreshDerived(item);

        return true;
    }
}
