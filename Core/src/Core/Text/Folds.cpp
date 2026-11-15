#include "corepch.h"

#include "Core/Text/Folds.h"

#include "Core/Base/Ascii.h"
#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Indent.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/TextChange.h"
#include "Core/Text/TextDocument.h"

#include <algorithm>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace Core {

	namespace {
		constexpr std::string_view kOpeningBrackets = "([{";
		// What a line that only closes a block may hold: its brackets and the punctuation that ends a statement.
		constexpr std::string_view kClosingLineCharacters = ")]};, \t";

		struct LineShape
		{
			bool IsBlank = true;
			std::size_t Depth = 0;
			bool IsLoneOpeningBracket = false;
			bool OnlyClosesBrackets = false;
		};

		LineShape ShapeOf(const TextBuffer& buffer, std::size_t line, std::size_t tabWidth)
		{
			const std::string text = buffer.LineText(line);

			if (ContainsOnlySpacesAndTabs(text))
			{
				return LineShape{};
			}

			const std::size_t first = text.find_first_not_of(" \t");
			const std::size_t last = text.find_last_not_of(" \t");
			const bool lone = first == last && kOpeningBrackets.find(text[first]) != std::string_view::npos;

			const bool onlyCloses = kOpeningBrackets.find(text[first]) == std::string_view::npos &&
									kClosingLineCharacters.find(text[first]) != std::string_view::npos &&
									text.find_first_not_of(kClosingLineCharacters) == std::string::npos;

			return LineShape{.IsBlank = false,
							 .Depth = IndentDepth(text, tabWidth),
							 .IsLoneOpeningBracket = lone,
							 .OnlyClosesBrackets = onlyCloses};
		}

		std::optional<std::size_t> NextTextLine(const TextBuffer& buffer, std::size_t line)
		{
			for (std::size_t next = line + 1; next < buffer.GetLineCount(); ++next)
			{
				if (!ContainsOnlySpacesAndTabs(buffer.LineText(next)))
				{
					return next;
				}
			}

			return std::nullopt;
		}

		std::optional<std::size_t> PreviousTextLine(const TextBuffer& buffer, std::size_t line)
		{
			for (std::size_t previous = line; previous > 0;)
			{
				--previous;

				if (!ContainsOnlySpacesAndTabs(buffer.LineText(previous)))
				{
					return previous;
				}
			}

			return std::nullopt;
		}

		// The last line with text below the line that is indented deeper than the depth.
		std::size_t LastDeeperLine(const TextBuffer& buffer, std::size_t line, std::size_t depth, std::size_t tabWidth)
		{
			std::size_t last = line;

			for (std::size_t next = line + 1; next < buffer.GetLineCount(); ++next)
			{
				const LineShape shape = ShapeOf(buffer, next, tabWidth);

				if (shape.IsBlank)
				{
					continue;
				}

				if (shape.Depth <= depth)
				{
					break;
				}

				last = next;
			}

			return last;
		}

		std::optional<Fold> DeeperBlockBelow(const TextBuffer& buffer, std::size_t line, std::size_t depth,
											 std::size_t tabWidth)
		{
			const std::optional<std::size_t> next = NextTextLine(buffer, line);

			if (!next || ShapeOf(buffer, *next, tabWidth).Depth <= depth)
			{
				return std::nullopt;
			}

			const std::size_t last = LastDeeperLine(buffer, line, depth, tabWidth);

			// The line that closes the block goes with it, so a folded block takes one line and its marker stands for
			// all of it.
			if (const std::optional<std::size_t> closing = NextTextLine(buffer, last))
			{
				const LineShape closingShape = ShapeOf(buffer, *closing, tabWidth);

				if (closingShape.OnlyClosesBrackets && closingShape.Depth == depth)
				{
					return Fold{.Header = line, .Last = *closing};
				}
			}

			return Fold{.Header = line, .Last = last};
		}

		bool Hides(const Fold& fold, std::size_t line)
		{
			return line > fold.Header && line <= fold.Last;
		}

		bool Overlaps(const Fold& fold, std::size_t firstLine, std::size_t lastLine)
		{
			return fold.Header <= lastLine && firstLine <= fold.Last;
		}

		// The folds after one change of the text. A change confined to the header's own line keeps its fold; one
		// that reaches the lines the fold hides, or adds or removes a line at the header, opens it.
		bool ShiftFolds(std::vector<Fold>& folds, const TextChange& change)
		{
			const std::size_t lastTouched = change.FirstLine + change.RemovedLines;
			const bool withinOneLine = change.RemovedLines == 0 && change.InsertedLines == 0;
			const std::size_t added = change.InsertedLines;
			const std::size_t removed = change.RemovedLines;
			const std::size_t sizeBefore = folds.size();

			std::erase_if(folds, [&](const Fold& fold) {
				const bool onlyTheHeader = withinOneLine && change.FirstLine == fold.Header;

				return !onlyTheHeader && Overlaps(fold, change.FirstLine, lastTouched);
			});

			for (Fold& fold : folds)
			{
				if (fold.Header > lastTouched)
				{
					fold.Header = fold.Header + added - removed;
					fold.Last = fold.Last + added - removed;
				}
			}

			return folds.size() != sizeBefore;
		}

		void RevealEveryLine(OpenItem& item)
		{
			for (std::size_t line = 0; line < item.Rows.GetLineCount(); ++line)
			{
				item.Rows.SetRowsOf(line, item.Rows.BreaksOf(line).size() + 1);
			}
		}

		void RevealLines(OpenItem& item, const Fold& fold)
		{
			for (std::size_t line = fold.Header + 1; line <= fold.Last && line < item.Rows.GetLineCount(); ++line)
			{
				item.Rows.SetRowsOf(line, item.Rows.BreaksOf(line).size() + 1);
			}
		}

		// Brings the folds up to the text; true when one had to open, whose lines then need showing again.
		bool CatchUpFolds(OpenItem& item, const TextBuffer& buffer)
		{
			const std::uint64_t now = buffer.GetChangeSequence();

			if (item.FoldsSeenChange == now)
			{
				return false;
			}

			bool opened = false;
			const std::optional<std::span<const TextChange>> changes = buffer.ChangesSince(item.FoldsSeenChange);

			if (!changes)
			{
				opened = !item.Folds.empty();
				item.Folds.clear();
			}
			else
			{
				for (const TextChange& change : *changes)
				{
					if (change.WholeContent)
					{
						opened = opened || !item.Folds.empty();
						item.Folds.clear();
						continue;
					}

					opened = ShiftFolds(item.Folds, change) || opened;
				}
			}

			item.FoldsSeenChange = now;

			if (opened)
			{
				++item.FoldRevision;
			}

			return opened;
		}

		bool ByHeader(const Fold& left, const Fold& right)
		{
			return left.Header < right.Header || (left.Header == right.Header && left.Last < right.Last);
		}

		// Kept sorted by header as they go in, so folding every block of a large file stays linear per fold.
		void AddFold(OpenItem& item, const Fold& fold)
		{
			const std::vector<Fold>::iterator at = std::lower_bound(item.Folds.begin(), item.Folds.end(), fold, ByHeader);

			if (at != item.Folds.end() && *at == fold)
			{
				return;
			}

			item.Folds.insert(at, fold);
			++item.FoldRevision;
		}

		void RemoveFolds(OpenItem& item, const std::vector<Fold>& removed)
		{
			for (const Fold& fold : removed)
			{
				std::erase(item.Folds, fold);
			}

			++item.FoldRevision;

			for (const Fold& fold : removed)
			{
				RevealLines(item, fold);
			}

			HideFoldedRows(item);
		}

		void PlaceCaretAt(OpenItem& item, std::size_t offset)
		{
			item.Caret.Offset = offset;
			item.Caret.Anchor = offset;
			item.Caret.DesiredColumn = NoDesiredColumn;
			item.RowAimCaretOffset = NoRowAim;
		}

		std::size_t LineEndOffset(const TextBuffer& buffer, std::size_t line)
		{
			return buffer.LineStart(line) + buffer.LineLength(line);
		}

		// The innermost foldable block that hides the line, found by walking up to each shallower line in turn.
		std::optional<Fold> EnclosingFold(const TextBuffer& buffer, std::size_t line, std::size_t tabWidth)
		{
			const std::optional<std::size_t> textLine =
				ContainsOnlySpacesAndTabs(buffer.LineText(line)) ? NextTextLine(buffer, line) : std::optional{line};

			if (!textLine)
			{
				return std::nullopt;
			}

			std::size_t depth = ShapeOf(buffer, *textLine, tabWidth).Depth;

			for (std::size_t above = std::min(line, *textLine); above > 0;)
			{
				--above;

				const LineShape shape = ShapeOf(buffer, above, tabWidth);

				if (shape.IsBlank || shape.Depth >= depth)
				{
					continue;
				}

				depth = shape.Depth;
				std::size_t header = above;

				if (shape.IsLoneOpeningBracket)
				{
					const std::optional<std::size_t> previous = PreviousTextLine(buffer, above);

					if (previous && ShapeOf(buffer, *previous, tabWidth).Depth == shape.Depth)
					{
						header = *previous;
					}
				}

				const std::optional<Fold> candidate = FoldableAt(buffer, header, tabWidth);

				if (candidate && Hides(*candidate, line))
				{
					return candidate;
				}
			}

			return std::nullopt;
		}

		// After a fold closes over the caret, it goes to the end of the header in view.
		void MoveCaretOutOfFolds(OpenItem& item)
		{
			const TextBuffer& buffer = item.Document->GetBuffer();
			const std::size_t line = buffer.ToPosition(std::min(item.Caret.Offset, buffer.GetLength())).Line;

			if (const std::optional<Fold> hiding = FoldHiding(item, line))
			{
				PlaceCaretAt(item, LineEndOffset(buffer, hiding->Header));
			}
		}
	}

	std::optional<Fold> FoldableAt(const TextBuffer& buffer, std::size_t line, std::size_t tabWidth)
	{
		if (line >= buffer.GetLineCount())
		{
			return std::nullopt;
		}

		const LineShape shape = ShapeOf(buffer, line, tabWidth);

		if (shape.IsBlank)
		{
			return std::nullopt;
		}

		if (shape.IsLoneOpeningBracket)
		{
			const std::optional<std::size_t> previous = PreviousTextLine(buffer, line);

			if (previous && ShapeOf(buffer, *previous, tabWidth).Depth == shape.Depth)
			{
				return std::nullopt;
			}
		}

		if (std::optional<Fold> block = DeeperBlockBelow(buffer, line, shape.Depth, tabWidth))
		{
			return block;
		}

		const std::optional<std::size_t> next = NextTextLine(buffer, line);

		if (!next)
		{
			return std::nullopt;
		}

		const LineShape nextShape = ShapeOf(buffer, *next, tabWidth);

		if (!nextShape.IsLoneOpeningBracket || nextShape.Depth != shape.Depth)
		{
			return std::nullopt;
		}

		const std::optional<Fold> bracketBlock = DeeperBlockBelow(buffer, *next, nextShape.Depth, tabWidth);

		if (!bracketBlock)
		{
			return std::nullopt;
		}

		return Fold{.Header = line, .Last = bracketBlock->Last};
	}

	bool CanFoldAt(const TextBuffer& buffer, std::size_t line, std::size_t tabWidth)
	{
		if (line >= buffer.GetLineCount())
		{
			return false;
		}

		const LineShape shape = ShapeOf(buffer, line, tabWidth);

		if (shape.IsBlank)
		{
			return false;
		}

		if (shape.IsLoneOpeningBracket)
		{
			const std::optional<std::size_t> previous = PreviousTextLine(buffer, line);

			if (previous && ShapeOf(buffer, *previous, tabWidth).Depth == shape.Depth)
			{
				return false;
			}
		}

		const std::optional<std::size_t> next = NextTextLine(buffer, line);

		if (!next)
		{
			return false;
		}

		const LineShape nextShape = ShapeOf(buffer, *next, tabWidth);

		if (nextShape.Depth > shape.Depth)
		{
			return true;
		}

		if (!nextShape.IsLoneOpeningBracket || nextShape.Depth != shape.Depth)
		{
			return false;
		}

		const std::optional<std::size_t> afterBracket = NextTextLine(buffer, *next);

		return afterBracket && ShapeOf(buffer, *afterBracket, tabWidth).Depth > shape.Depth;
	}

	std::optional<Fold> FoldAround(const TextBuffer& buffer, std::size_t line, std::size_t tabWidth)
	{
		if (const std::optional<Fold> own = FoldableAt(buffer, line, tabWidth))
		{
			return own;
		}

		// A line that closes a block belongs to the fold of the line at its depth that opened it.
		if (ShapeOf(buffer, line, tabWidth).OnlyClosesBrackets)
		{
			const std::size_t depth = ShapeOf(buffer, line, tabWidth).Depth;

			for (std::size_t above = line; above > 0;)
			{
				--above;

				const LineShape shape = ShapeOf(buffer, above, tabWidth);

				if (shape.IsBlank || shape.Depth > depth)
				{
					continue;
				}

				if (shape.Depth < depth)
				{
					break;
				}

				const std::optional<Fold> opened = FoldableAt(buffer, above, tabWidth);

				if (opened && Hides(*opened, line))
				{
					return opened;
				}
			}
		}

		// A lone bracket below its declaration belongs to the declaration's fold.
		if (const std::optional<std::size_t> previous = PreviousTextLine(buffer, line))
		{
			const std::optional<Fold> declaration = FoldableAt(buffer, *previous, tabWidth);

			if (declaration && Hides(*declaration, line) && ShapeOf(buffer, line, tabWidth).IsLoneOpeningBracket)
			{
				return declaration;
			}
		}

		return EnclosingFold(buffer, line, tabWidth);
	}

	bool IsFoldHeader(const OpenItem& item, std::size_t line)
	{
		return std::any_of(item.Folds.begin(), item.Folds.end(), [line](const Fold& fold) { return fold.Header == line; });
	}

	bool IsLineFolded(const OpenItem& item, std::size_t line)
	{
		return std::any_of(item.Folds.begin(), item.Folds.end(), [line](const Fold& fold) { return Hides(fold, line); });
	}

	std::optional<Fold> FoldHiding(const OpenItem& item, std::size_t line)
	{
		std::optional<Fold> outermost;

		for (const Fold& fold : item.Folds)
		{
			if (Hides(fold, line) && (!outermost || fold.Header < outermost->Header))
			{
				outermost = fold;
			}
		}

		return outermost;
	}

	bool ToggleFold(OpenItem& item, std::size_t line)
	{
		if (!item.Document)
		{
			return false;
		}

		HideFoldedRows(item);

		std::vector<Fold> atHeader;

		for (const Fold& fold : item.Folds)
		{
			if (fold.Header == line)
			{
				atHeader.push_back(fold);
			}
		}

		if (!atHeader.empty())
		{
			RemoveFolds(item, atHeader);
			return true;
		}

		const std::optional<Fold> foldable = FoldableAt(item.Document->GetBuffer(), line, item.TabWidth);

		if (!foldable)
		{
			return false;
		}

		AddFold(item, *foldable);
		HideFoldedRows(item);
		MoveCaretOutOfFolds(item);

		return true;
	}

	bool FoldAtCaret(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		HideFoldedRows(item);

		const TextBuffer& buffer = item.Document->GetBuffer();
		const std::size_t line = buffer.ToPosition(item.Caret.Offset).Line;

		// Folding again from a folded header folds the block around it, as a second press goes one level out.
		std::optional<Fold> around = FoldAround(buffer, line, item.TabWidth);

		while (around && IsFoldHeader(item, around->Header))
		{
			around = EnclosingFold(buffer, around->Header, item.TabWidth);
		}

		if (!around)
		{
			return false;
		}

		AddFold(item, *around);
		HideFoldedRows(item);
		PlaceCaretAt(item, LineEndOffset(buffer, around->Header));
		item.PendingScroll = true;

		return true;
	}

	bool UnfoldAtCaret(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		HideFoldedRows(item);

		const std::size_t line = item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line;
		std::vector<Fold> opened;

		for (const Fold& fold : item.Folds)
		{
			if (fold.Header == line || Hides(fold, line))
			{
				opened.push_back(fold);
			}
		}

		if (opened.empty())
		{
			return false;
		}

		RemoveFolds(item, opened);

		return true;
	}

	bool FoldAll(OpenItem& item)
	{
		if (!item.Document)
		{
			return false;
		}

		HideFoldedRows(item);

		const TextBuffer& buffer = item.Document->GetBuffer();
		std::vector<Fold> every;

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			if (const std::optional<Fold> foldable = FoldableAt(buffer, line, item.TabWidth))
			{
				every.push_back(*foldable);
			}
		}

		std::vector<Fold> merged;
		merged.reserve(every.size() + item.Folds.size());
		std::merge(item.Folds.begin(), item.Folds.end(), every.begin(), every.end(), std::back_inserter(merged), ByHeader);
		merged.erase(std::unique(merged.begin(), merged.end()), merged.end());

		if (merged.size() == item.Folds.size())
		{
			return false;
		}

		item.Folds = std::move(merged);
		++item.FoldRevision;

		HideFoldedRows(item);
		MoveCaretOutOfFolds(item);

		return true;
	}

	bool UnfoldAll(OpenItem& item)
	{
		if (item.Folds.empty())
		{
			return false;
		}

		item.Folds.clear();
		++item.FoldRevision;
		RevealEveryLine(item);

		return true;
	}

	void UnfoldAround(OpenItem& item, std::size_t line)
	{
		std::vector<Fold> hiding;

		for (const Fold& fold : item.Folds)
		{
			if (Hides(fold, line))
			{
				hiding.push_back(fold);
			}
		}

		if (!hiding.empty())
		{
			RemoveFolds(item, hiding);
		}
	}

	void KeepCaretOutOfFolds(OpenItem& item, std::size_t caretBefore)
	{
		if (!item.Document || item.Folds.empty())
		{
			return;
		}

		const TextBuffer& buffer = item.Document->GetBuffer();
		const std::size_t line = buffer.ToPosition(std::min(item.Caret.Offset, buffer.GetLength())).Line;
		const std::optional<Fold> hiding = FoldHiding(item, line);

		if (!hiding)
		{
			return;
		}

		const std::size_t lineBefore = buffer.ToPosition(std::min(caretBefore, buffer.GetLength())).Line;
		const bool stepFromHeader = lineBefore == hiding->Header;
		const bool stepFromBelow = lineBefore == hiding->Last + 1;
		const bool foldEndsTheDocument = hiding->Last + 1 >= buffer.GetLineCount();

		// A selection keeps its anchor and only its moving end steps over the fold.
		const auto stepTo = [&item](std::size_t offset) {
			const bool keepAnchor = HasSelection(item.Caret);
			item.Caret.Offset = offset;
			item.Caret.Anchor = keepAnchor ? item.Caret.Anchor : offset;
			item.Caret.DesiredColumn = NoDesiredColumn;
			item.RowAimCaretOffset = NoRowAim;
		};

		if (stepFromHeader && !foldEndsTheDocument)
		{
			stepTo(buffer.LineStart(hiding->Last + 1));
			return;
		}

		// Past a fold that ends the document there is nowhere to go, so the caret stays on the header.
		if (stepFromBelow || stepFromHeader)
		{
			stepTo(LineEndOffset(buffer, hiding->Header));
			return;
		}

		UnfoldAround(item, line);
	}

	void HideFoldedRows(OpenItem& item)
	{
		if (!item.Document)
		{
			return;
		}

		const TextBuffer& buffer = item.Document->GetBuffer();

		// WARNING: Rows that are behind the text, as a background rebuild can leave them for a frame, number lines
		// differently from the folds. The folds wait for them, so a fold an edit opened has its lines shown again
		// on rows that hold them.
		if (item.Rows.GetLineCount() != buffer.GetLineCount())
		{
			return;
		}

		const std::size_t foldsBefore = item.Folds.size();
		const bool opened = CatchUpFolds(item, buffer);

		std::erase_if(item.Folds, [&](const Fold& fold) { return fold.Last >= buffer.GetLineCount(); });

		if (opened || item.Folds.size() != foldsBefore)
		{
			++item.FoldRevision;
			RevealEveryLine(item);
		}

		for (const Fold& fold : item.Folds)
		{
			for (std::size_t line = fold.Header + 1; line <= fold.Last; ++line)
			{
				item.Rows.SetRowsOf(line, RowMap::kNoRows);
			}
		}
	}
}
