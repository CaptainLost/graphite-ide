#include "corepch.h"

#include "Core/Text/Editing.h"

#include "Core/Base/Ascii.h"
#include "Core/Jobs/CancellationPacing.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Indent.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextChange.h"
#include "Core/Text/TextDocument.h"

#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Core {

	namespace {
		constexpr std::size_t kPlaceholderIndentDepth = 0;

		struct LineBlock
		{
			std::size_t FirstLine = 0;
			std::size_t LastLine = 0;
		};

		bool IsBlankLine(const TextBuffer& buffer, std::size_t line)
		{
			return ContainsOnlySpacesAndTabs(buffer.LineText(line));
		}

		LineBlock WidenOverBlankNeighbours(const TextBuffer& buffer, std::size_t firstLine,
										   std::size_t lastLine)
		{
			const std::size_t lastLineOfBuffer = buffer.GetLineCount() - 1;
			std::size_t widenedFirst = std::min(firstLine, lastLineOfBuffer);

			// A blank line takes its depth from the nearest text above and below it, so the blank
			// run beside the edit changes with it even when the edited line itself is not blank.
			while (widenedFirst > 0 && IsBlankLine(buffer, widenedFirst - 1))
			{
				--widenedFirst;
			}

			std::size_t widenedLast = std::min(lastLine, lastLineOfBuffer);

			while (widenedLast < lastLineOfBuffer && IsBlankLine(buffer, widenedLast + 1))
			{
				++widenedLast;
			}

			return LineBlock{.FirstLine = widenedFirst, .LastLine = widenedLast};
		}

		// resize() here would pad or truncate only the tail, pairing every surviving depth below the edit with the wrong line.
		void SpliceDepthsAtEditSite(OpenItem& item, std::size_t firstLine, std::size_t currentLineCount)
		{
			const std::size_t previousLineCount = item.IndentDepths.size();
			const std::size_t editSite = std::min(firstLine, previousLineCount);

			if (currentLineCount > previousLineCount)
			{
				item.IndentDepths.insert(
					item.IndentDepths.begin() + static_cast<std::ptrdiff_t>(editSite),
					currentLineCount - previousLineCount, kPlaceholderIndentDepth);
			}
			else if (currentLineCount < previousLineCount)
			{
				const std::size_t spliceEnd =
					std::min(editSite + (previousLineCount - currentLineCount), previousLineCount);

				item.IndentDepths.erase(
					item.IndentDepths.begin() + static_cast<std::ptrdiff_t>(editSite),
					item.IndentDepths.begin() + static_cast<std::ptrdiff_t>(spliceEnd));
			}
		}

		void RecomputeDepths(OpenItem& item, const TextBuffer& buffer, std::size_t firstLine,
							 std::size_t lastLine)
		{
			const std::vector<std::size_t> depths = IndentDepths(buffer, item.TabWidth, firstLine, lastLine);

			for (std::size_t line = firstLine; line <= lastLine; ++line)
			{
				item.IndentDepths[line] = depths[line - firstLine];
			}
		}

		void UpdateIndentDepths(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
		{
			const TextBuffer& buffer = item.Document->GetBuffer();

			SpliceDepthsAtEditSite(item, firstLine, buffer.GetLineCount());

			const LineBlock widened = WidenOverBlankNeighbours(buffer, firstLine, lastLine);

			RecomputeDepths(item, buffer, widened.FirstLine, widened.LastLine);
		}

		void SpliceRowsAtEditSite(OpenItem& item, const TextBuffer& buffer, std::size_t firstLine)
		{
			const std::size_t previousLineCount = item.Rows.GetLineCount();
			const std::size_t currentLineCount = buffer.GetLineCount();
			const std::size_t editSite = std::min(firstLine, previousLineCount);

			if (currentLineCount > previousLineCount)
			{
				item.Rows.InsertLines(buffer, editSite, currentLineCount - previousLineCount);
			}
			else if (currentLineCount < previousLineCount)
			{
				item.Rows.RemoveLines(editSite, previousLineCount - currentLineCount);
			}
		}

		void RecordLineIfWidest(OpenItem& item, const TextBuffer& buffer, std::size_t line)
		{
			const std::size_t width = ColumnCount(buffer.LineText(line), item.TabWidth);

			if (width > item.WidestColumn)
			{
				item.WidestColumn = width;
				item.WidestLine = line;
			}
		}

		void ExtendWidestLine(OpenItem& item, const TextBuffer& buffer, std::size_t firstLine,
							  std::size_t lastLine)
		{
			for (std::size_t line = firstLine; line <= lastLine && line < buffer.GetLineCount();
				 ++line)
			{
				RecordLineIfWidest(item, buffer, line);
			}
		}

		bool HasNarrowedBelowWidest(const OpenItem& item, const TextBuffer& buffer)
		{
			return ColumnCount(buffer.LineText(item.WidestLine), item.TabWidth) < item.WidestColumn;
		}

		void UpdateLineCachesAfterEdit(OpenItem& item, std::size_t firstLine, std::size_t lastLine)
		{
			const TextBuffer& buffer = item.Document->GetBuffer();

			const bool widestLineNoLongerExists = item.WidestLine >= buffer.GetLineCount();

			// WARNING: Recomputing visits every line of the document, so it is kept for the edits that can
			// leave the recorded line wrong: one that shifts the lines below it, or one that narrowed it. A
			// line that is only below the edit, or only wider than before, is left alone; without this,
			// typing anywhere above the widest line of a large file recomputed the whole file per keystroke.
			const bool lineCountChanged = buffer.GetLineCount() != item.Rows.GetLineCount();
			const bool widestLineMayHaveMoved = lineCountChanged && item.WidestLine >= firstLine;
			const bool widestLineWasEdited = item.WidestLine >= firstLine && item.WidestLine <= lastLine;

			if (widestLineNoLongerExists || widestLineMayHaveMoved ||
				(widestLineWasEdited && HasNarrowedBelowWidest(item, buffer)))
			{
				RecomputeLineCaches(item);
				return;
			}

			ExtendWidestLine(item, buffer, firstLine, lastLine);
			UpdateIndentDepths(item, firstLine, lastLine);
			SpliceRowsAtEditSite(item, buffer, firstLine);

			for (std::size_t line = firstLine; line <= lastLine && line < buffer.GetLineCount();
				 ++line)
			{
				item.Rows.UpdateLine(buffer, line);
			}
		}

	}

	std::optional<WidestLineFound> FindWidestLineUnlessCancelled(const TextBuffer& buffer, std::size_t tabWidth,
															const CancellationToken& cancellation)
	{
		WidestLineFound widest;
		CancellationPacing pacing{cancellation};

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			if (pacing.CancelledBefore(buffer.LineLength(line)))
			{
				return std::nullopt;
			}

			std::string copyWhenSplit;
			const std::optional<std::size_t> countedWidth =
				ColumnCountUnlessCancelled(buffer.LineTextView(line, copyWhenSplit), tabWidth, cancellation);

			if (!countedWidth)
			{
				return std::nullopt;
			}

			if (*countedWidth > widest.Column)
			{
				widest = WidestLineFound{.Line = line, .Column = *countedWidth};
			}
		}

		return widest;
	}

	LineCaches ComputeLineCaches(const TextBuffer& buffer, std::size_t tabWidth, std::size_t columnsPerRow)
	{
		const CancellationToken neverCancelled;

		return *ComputeLineCachesUnlessCancelled(buffer, tabWidth, columnsPerRow, neverCancelled);
	}

	std::optional<LineCaches> ComputeLineCachesUnlessCancelled(const TextBuffer& buffer, std::size_t tabWidth,
															   std::size_t columnsPerRow, const CancellationToken& cancellation)
	{
		std::optional<std::vector<std::size_t>> depths = IndentDepthsUnlessCancelled(buffer, tabWidth, cancellation);

		if (!depths)
		{
			return std::nullopt;
		}

		const std::optional<WidestLineFound> widest = FindWidestLineUnlessCancelled(buffer, tabWidth, cancellation);

		if (!widest)
		{
			return std::nullopt;
		}

		LineCaches caches;
		caches.TabWidth = tabWidth;
		caches.IndentDepths = std::move(*depths);
		caches.WidestColumn = widest->Column;
		caches.WidestLine = widest->Line;

		if (!caches.Rows.RebuildUnlessCancelled(buffer, columnsPerRow, tabWidth, cancellation))
		{
			return std::nullopt;
		}

		caches.SeenChange = buffer.GetChangeSequence();

		return caches;
	}

	void AdoptLineCaches(OpenItem& item, LineCaches caches)
	{
		item.TabWidth = caches.TabWidth;
		item.IndentDepths = std::move(caches.IndentDepths);
		item.WidestColumn = caches.WidestColumn;
		item.WidestLine = caches.WidestLine;
		item.Rows = std::move(caches.Rows);
		item.SeenChange = caches.SeenChange;
	}

	void RecomputeLineCaches(OpenItem& item)
	{
		// Rebuilt at the width already in force, not at kNoWrap: the editor rewraps only when its
		// width changes, so a document on screen would stay unwrapped until the next resize.
		const std::size_t columnsPerRowInForce = item.Rows.GetColumnsPerRow();

		AdoptLineCaches(item, ComputeLineCaches(item.Document->GetBuffer(), item.TabWidth, columnsPerRowInForce));
	}

	void CatchUpWithDocument(OpenItem& item, CaretFollow follow)
	{
		if (!item.Document)
		{
			return;
		}

		const TextBuffer& buffer = item.Document->GetBuffer();

		if (item.SeenChange == buffer.GetChangeSequence())
		{
			return;
		}

		const std::optional<std::span<const TextChange>> changes = buffer.ChangesSince(item.SeenChange);
		const bool recomputeAll = !changes || LinesChangedBy(*changes).Whole;

		if (follow == CaretFollow::Shift)
		{
			if (recomputeAll)
			{
				item.Caret = PlaceAt(buffer, item.Caret, item.Caret.Offset, SelectionChange::Collapse, item.TabWidth);
			}
			else
			{
				item.Caret.Offset = ShiftOffset(item.Caret.Offset, *changes);
				item.Caret.Anchor = ShiftOffset(item.Caret.Anchor, *changes);
				item.Caret.DesiredColumn = NoDesiredColumn;
			}

			// The vertical aim was measured against text that has moved. It is cleared rather
			// than converted, so the next Up or Down starts from where the caret is now.
			item.RowAimCaretOffset = NoRowAim;
		}

		if (recomputeAll)
		{
			RecomputeLineCaches(item);
			return;
		}

		const ChangedLines lines = LinesChangedBy(*changes);
		UpdateLineCachesAfterEdit(item, lines.FirstLine, lines.LastLine);
		item.SeenChange = buffer.GetChangeSequence();
	}

	bool InsertText(OpenItem& item, std::string_view utf8)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = InsertText(*item.Document, item.Caret, utf8);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}
	bool DeleteBackward(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = DeleteBackward(*item.Document, item.Caret, item.TabWidth);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}

	bool DeleteForward(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = DeleteForward(*item.Document, item.Caret, item.TabWidth);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}

	bool DeleteWordBackward(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = DeleteWordBackward(*item.Document, item.Caret, item.TabWidth);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}

	bool DeleteWordForward(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = DeleteWordForward(*item.Document, item.Caret, item.TabWidth);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}

	bool Undo(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = Undo(*item.Document, item.Caret);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}

	bool Redo(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = Redo(*item.Document, item.Caret);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}

	bool InsertNewline(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = InsertNewline(*item.Document, item.Caret);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}

	bool Indent(OpenItem& item, IndentStyle style)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = Indent(*item.Document, item.Caret, style);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}

	bool Outdent(OpenItem& item, IndentStyle style)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = Outdent(*item.Document, item.Caret, style);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}

	bool ToggleLineComment(OpenItem& item, std::string_view commentPrefix)
	{
		if (!item.Document)
		{
			return false;
		}

		const bool changed = ToggleLineComment(*item.Document, item.Caret, commentPrefix);
		CatchUpWithDocument(item, CaretFollow::AlreadyPlaced);

		return changed;
	}
}
