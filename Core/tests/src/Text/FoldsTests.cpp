#include <doctest/doctest.h>

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/Folds.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/TextDocument.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace {
	constexpr std::size_t kTabWidth = 4;
	constexpr Core::IndentStyle kFourSpaces{.UseSpaces = true, .Width = 4};

	constexpr std::string_view kSameLineBraces = "class A {\n"
												 "    void f() {\n"
												 "        one();\n"
												 "\n"
												 "        two();\n"
												 "    }\n"
												 "\n"
												 "    int x;\n"
												 "}\n";

	constexpr std::string_view kBracesBelow = "public class A\n"
											  "{\n"
											  "    void F()\n"
											  "    {\n"
											  "        One();\n"
											  "    }\n"
											  "}\n";

	Core::OpenItem ItemWith(std::string_view content)
	{
		Core::OpenItem item;

		Core::Result<Core::TextDocument> document = Core::TextDocument::FromContent(std::string{content}, "test.cs");
		REQUIRE(document);

		item.Document = std::make_shared<Core::TextDocument>(std::move(document).GetValue());
		Core::RecomputeLineCaches(item);

		return item;
	}

	Core::TextBuffer BufferWith(std::string_view content)
	{
		return Core::TextBuffer{std::string{content}};
	}

	void PlaceCaretAt(Core::OpenItem& item, std::size_t line, std::size_t column = 0)
	{
		const std::size_t offset = item.Document->GetBuffer().LineStart(line) + column;
		item.Caret.Offset = offset;
		item.Caret.Anchor = offset;
	}

	std::size_t CaretLine(const Core::OpenItem& item)
	{
		return item.Document->GetBuffer().ToPosition(item.Caret.Offset).Line;
	}
}

TEST_CASE("A line folds the deeper lines below it and the line that closes them, past the blank lines between")
{
	const Core::TextBuffer buffer = BufferWith(kSameLineBraces);

	CHECK(Core::FoldableAt(buffer, 0, kTabWidth) == Core::Fold{.Header = 0, .Last = 8});
	CHECK(Core::FoldableAt(buffer, 1, kTabWidth) == Core::Fold{.Header = 1, .Last = 5});
}

TEST_CASE("A line with nothing deeper below it, or a blank line, does not fold")
{
	const Core::TextBuffer buffer = BufferWith(kSameLineBraces);

	CHECK_FALSE(Core::FoldableAt(buffer, 2, kTabWidth).has_value());
	CHECK_FALSE(Core::FoldableAt(buffer, 3, kTabWidth).has_value());
	CHECK_FALSE(Core::FoldableAt(buffer, 8, kTabWidth).has_value());
}

TEST_CASE("A declaration with its brace on the next line folds the brace, its block and its closing brace, and the brace folds nothing")
{
	const Core::TextBuffer buffer = BufferWith(kBracesBelow);

	CHECK(Core::FoldableAt(buffer, 0, kTabWidth) == Core::Fold{.Header = 0, .Last = 6});
	CHECK_FALSE(Core::FoldableAt(buffer, 1, kTabWidth).has_value());
	CHECK(Core::FoldableAt(buffer, 2, kTabWidth) == Core::Fold{.Header = 2, .Last = 5});
	CHECK_FALSE(Core::FoldableAt(buffer, 3, kTabWidth).has_value());
}

TEST_CASE("The block around a line is the innermost one that hides it")
{
	const Core::TextBuffer buffer = BufferWith(kBracesBelow);

	CHECK(Core::FoldAround(buffer, 4, kTabWidth) == Core::Fold{.Header = 2, .Last = 5});
	CHECK(Core::FoldAround(buffer, 3, kTabWidth) == Core::Fold{.Header = 2, .Last = 5});
	CHECK(Core::FoldAround(buffer, 2, kTabWidth) == Core::Fold{.Header = 2, .Last = 5});
	CHECK(Core::FoldAround(buffer, 5, kTabWidth) == Core::Fold{.Header = 2, .Last = 5});
	CHECK(Core::FoldAround(buffer, 6, kTabWidth) == Core::Fold{.Header = 0, .Last = 6});
}

TEST_CASE("Folding a line hides its block's rows and unfolding shows them again")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	const std::size_t rowsBefore = item.Rows.GetTotalRows();

	REQUIRE(Core::ToggleFold(item, 1));
	CHECK(Core::IsFoldHeader(item, 1));
	CHECK(Core::IsLineFolded(item, 2));
	CHECK(Core::IsLineFolded(item, 5));
	CHECK_FALSE(Core::IsLineFolded(item, 6));
	CHECK(item.Rows.GetTotalRows() == rowsBefore - 4);
	CHECK(item.Rows.RowAt(2).Line == 6);

	REQUIRE(Core::ToggleFold(item, 1));
	CHECK(item.Folds.empty());
	CHECK(item.Rows.GetTotalRows() == rowsBefore);
}

TEST_CASE("A line that folds nothing refuses to fold")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);

	CHECK_FALSE(Core::ToggleFold(item, 2));
	CHECK(item.Folds.empty());
}

TEST_CASE("Unfolding an outer fold leaves the inner one folded")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	const std::size_t rowsBefore = item.Rows.GetTotalRows();

	REQUIRE(Core::ToggleFold(item, 1));
	REQUIRE(Core::ToggleFold(item, 0));
	CHECK(item.Rows.GetTotalRows() == rowsBefore - 8);

	REQUIRE(Core::ToggleFold(item, 0));
	CHECK(Core::IsFoldHeader(item, 1));
	CHECK(item.Rows.GetTotalRows() == rowsBefore - 4);
}

TEST_CASE("Folding over the caret moves it to the end of the header")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	PlaceCaretAt(item, 2, 4);

	REQUIRE(Core::ToggleFold(item, 1));

	CHECK(CaretLine(item) == 1);
	CHECK(item.Caret.Offset == item.Document->GetBuffer().LineStart(1) + item.Document->GetBuffer().LineLength(1));
}

TEST_CASE("Fold at the caret folds the block around it, and again folds the block around that")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	PlaceCaretAt(item, 2, 4);

	REQUIRE(Core::FoldAtCaret(item));
	CHECK(Core::IsFoldHeader(item, 1));
	CHECK(CaretLine(item) == 1);

	REQUIRE(Core::FoldAtCaret(item));
	CHECK(Core::IsFoldHeader(item, 0));
	CHECK(CaretLine(item) == 0);
}

TEST_CASE("Unfold at the caret opens the fold on its header line")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	REQUIRE(Core::ToggleFold(item, 1));
	PlaceCaretAt(item, 1);

	CHECK(Core::UnfoldAtCaret(item));
	CHECK(item.Folds.empty());
	CHECK_FALSE(Core::UnfoldAtCaret(item));
}

TEST_CASE("Fold all folds every block and unfold all opens them")
{
	Core::OpenItem item = ItemWith(kBracesBelow);
	const std::size_t rowsBefore = item.Rows.GetTotalRows();

	REQUIRE(Core::FoldAll(item));
	CHECK(Core::IsFoldHeader(item, 0));
	CHECK(Core::IsFoldHeader(item, 2));
	CHECK(item.Rows.GetTotalRows() == rowsBefore - 6);

	REQUIRE(Core::UnfoldAll(item));
	CHECK(item.Rows.GetTotalRows() == rowsBefore);
	CHECK_FALSE(Core::UnfoldAll(item));
}

TEST_CASE("Lines added above a fold move it down with its text")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	REQUIRE(Core::ToggleFold(item, 1));
	PlaceCaretAt(item, 0);

	REQUIRE(Core::InsertText(item, "// note\n"));

	CHECK(Core::IsFoldHeader(item, 2));
	CHECK(Core::IsLineFolded(item, 3));
	CHECK(Core::IsLineFolded(item, 6));
	CHECK_FALSE(Core::IsLineFolded(item, 7));
	CHECK(item.Rows.RowsOf(3) == Core::RowMap::kNoRows);
	CHECK(item.Rows.RowsOf(7) == 1);
}

TEST_CASE("Typing on a fold's header line keeps the fold")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	REQUIRE(Core::ToggleFold(item, 1));
	PlaceCaretAt(item, 1, 4);

	REQUIRE(Core::InsertText(item, "static "));

	CHECK(Core::IsFoldHeader(item, 1));
	CHECK(item.Rows.RowsOf(2) == Core::RowMap::kNoRows);
}

TEST_CASE("An edit that reaches into a fold opens it and shows its lines")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	const std::size_t rowsBefore = item.Rows.GetTotalRows();
	REQUIRE(Core::ToggleFold(item, 1));
	PlaceCaretAt(item, 1);
	item.Caret.Offset = item.Document->GetBuffer().LineStart(1) + item.Document->GetBuffer().LineLength(1);
	item.Caret.Anchor = item.Caret.Offset;

	REQUIRE(Core::InsertNewline(item, kFourSpaces));

	CHECK(item.Folds.empty());
	CHECK(item.Rows.GetTotalRows() == rowsBefore + 1);
}

TEST_CASE("A step from a fold's header into it lands below the fold, and a step back lands on the header")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	REQUIRE(Core::ToggleFold(item, 1));
	const Core::TextBuffer& buffer = item.Document->GetBuffer();
	const std::size_t headerEnd = buffer.LineStart(1) + buffer.LineLength(1);

	item.Caret.Offset = headerEnd + 1;
	item.Caret.Anchor = item.Caret.Offset;
	Core::KeepCaretOutOfFolds(item, headerEnd);
	CHECK(item.Caret.Offset == buffer.LineStart(6));

	item.Caret.Offset = buffer.LineStart(6) - 1;
	item.Caret.Anchor = item.Caret.Offset;
	Core::KeepCaretOutOfFolds(item, buffer.LineStart(6));
	CHECK(item.Caret.Offset == headerEnd);
	CHECK_FALSE(item.Folds.empty());
}

TEST_CASE("A caret that jumps into a fold opens it")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	const std::size_t rowsBefore = item.Rows.GetTotalRows();
	REQUIRE(Core::ToggleFold(item, 1));

	PlaceCaretAt(item, 3);
	Core::KeepCaretOutOfFolds(item, 0);

	CHECK(item.Folds.empty());
	CHECK(item.Rows.GetTotalRows() == rowsBefore);
}

TEST_CASE("Folds survive a full rebuild of the rows")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	REQUIRE(Core::ToggleFold(item, 1));
	const std::size_t foldedRows = item.Rows.GetTotalRows();

	Core::RecomputeLineCaches(item);

	CHECK(item.Rows.GetTotalRows() == foldedRows);
}

TEST_CASE("The quick question whether a line folds agrees with the block it folds")
{
	for (const std::string_view text : {kSameLineBraces, kBracesBelow})
	{
		const Core::TextBuffer buffer = BufferWith(text);

		for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
		{
			CAPTURE(line);
			CHECK(Core::CanFoldAt(buffer, line, kTabWidth) == Core::FoldableAt(buffer, line, kTabWidth).has_value());
		}
	}
}

TEST_CASE("Past the last row is the last line in view, not a folded line at the end of the document")
{
	Core::OpenItem item = ItemWith("def f():\n    a\n    b");
	REQUIRE(Core::ToggleFold(item, 0));

	CHECK(item.Rows.GetTotalRows() == 1);
	CHECK(item.Rows.RowAt(1).Line == 0);
	CHECK(item.Rows.RowAt(5).Line == 0);
}

TEST_CASE("A step into a fold that ends the document stays on its header and keeps the fold")
{
	Core::OpenItem item = ItemWith("def f():\n    a\n    b");
	REQUIRE(Core::ToggleFold(item, 0));
	const Core::TextBuffer& buffer = item.Document->GetBuffer();
	const std::size_t headerEnd = buffer.LineLength(0);

	item.Caret.Offset = headerEnd + 1;
	item.Caret.Anchor = item.Caret.Offset;
	Core::KeepCaretOutOfFolds(item, headerEnd);

	CHECK(item.Caret.Offset == headerEnd);
	CHECK(Core::IsFoldHeader(item, 0));
}

TEST_CASE("A selection stepping over a fold keeps its anchor and does not open the fold")
{
	Core::OpenItem item = ItemWith(kSameLineBraces);
	REQUIRE(Core::ToggleFold(item, 1));
	const Core::TextBuffer& buffer = item.Document->GetBuffer();
	const std::size_t headerStart = buffer.LineStart(1);
	const std::size_t headerEnd = headerStart + buffer.LineLength(1);

	item.Caret.Anchor = headerStart;
	item.Caret.Offset = headerEnd + 1;
	Core::KeepCaretOutOfFolds(item, headerEnd);

	CHECK(item.Caret.Anchor == headerStart);
	CHECK(item.Caret.Offset == buffer.LineStart(6));
	CHECK(Core::IsFoldHeader(item, 1));
}

TEST_CASE("Folding all twice adds nothing the second time, and every fold counts as a change of the folds")
{
	Core::OpenItem item = ItemWith(kBracesBelow);
	const std::uint64_t revisionBefore = item.FoldRevision;

	REQUIRE(Core::FoldAll(item));
	CHECK(item.FoldRevision != revisionBefore);
	CHECK_FALSE(Core::FoldAll(item));
	CHECK(item.Folds.size() == 2);
}

TEST_CASE("Only a line that does nothing but close brackets goes into the fold")
{
	const Core::TextBuffer statement = BufferWith("var x = new[]\n{\n    1,\n};\nnext();\n");
	CHECK(Core::FoldableAt(statement, 0, kTabWidth) == Core::Fold{.Header = 0, .Last = 3});

	const Core::TextBuffer elseBranch = BufferWith("if (a) {\n    b();\n} else {\n    c();\n}\n");
	CHECK(Core::FoldableAt(elseBranch, 0, kTabWidth) == Core::Fold{.Header = 0, .Last = 1});
	CHECK(Core::FoldableAt(elseBranch, 2, kTabWidth) == Core::Fold{.Header = 2, .Last = 4});

	const Core::TextBuffer python = BufferWith("def f():\n    a\nb\n");
	CHECK(Core::FoldableAt(python, 0, kTabWidth) == Core::Fold{.Header = 0, .Last = 1});
}
