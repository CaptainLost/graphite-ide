#pragma once

#include <cstddef>
#include <optional>

namespace Core {

	struct OpenItem;
	class TextBuffer;

	// A folded block: its header line stays in view and the lines after it, through Last, are hidden.
	struct Fold
	{
		std::size_t Header = 0;
		std::size_t Last = 0;

		friend bool operator==(const Fold&, const Fold&) = default;
	};

	// The block a fold at this line would hide, found by indentation so it works for every language: the lines
	// after it that are indented deeper, without the blank lines at its end. A line followed by a lone opening
	// bracket at its own depth, as a declaration with its brace on the next line, folds the bracket's block with
	// it, and the bracket line is then no header of its own.
	std::optional<Fold> FoldableAt(const TextBuffer& buffer, std::size_t line, std::size_t tabWidth);

	// FoldableAt without finding where the block ends, so cheap enough to ask for every row on screen.
	bool CanFoldAt(const TextBuffer& buffer, std::size_t line, std::size_t tabWidth);

	// The innermost foldable block holding the line, the line's own block first.
	std::optional<Fold> FoldAround(const TextBuffer& buffer, std::size_t line, std::size_t tabWidth);

	bool IsFoldHeader(const OpenItem& item, std::size_t line);
	bool IsLineFolded(const OpenItem& item, std::size_t line);

	// The outermost fold hiding the line.
	std::optional<Fold> FoldHiding(const OpenItem& item, std::size_t line);

	// Folds the block at the header line, or opens it when it is folded. False when nothing folds there.
	bool ToggleFold(OpenItem& item, std::size_t line);

	// Folds the innermost block holding the caret's line and puts the caret on its header.
	bool FoldAtCaret(OpenItem& item);

	// Opens the fold whose header holds the caret, or every fold hiding the caret's line.
	bool UnfoldAtCaret(OpenItem& item);

	bool FoldAll(OpenItem& item);
	bool UnfoldAll(OpenItem& item);

	// Opens every fold hiding the line.
	void UnfoldAround(OpenItem& item, std::size_t line);

	// Keeps the caret on lines in view. A step from a fold's header into it goes on to the line below the fold, a
	// step back into it from that line goes to the end of the header; a caret that lands inside a fold any other
	// way, by a search or a jump, opens it.
	void KeepCaretOutOfFolds(OpenItem& item, std::size_t caretBefore);

	// Brings the folds up to the document's text and hides their lines in the item's rows. Called whenever the rows
	// are rebuilt or edited; an edit that reaches into a fold opens it, one above it moves it, and an edit within its
	// header line leaves it.
	void HideFoldedRows(OpenItem& item);
}
