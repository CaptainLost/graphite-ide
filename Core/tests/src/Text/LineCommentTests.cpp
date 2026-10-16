#include "Core/Text/Editing.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextDocument.h"

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <string_view>

namespace {
	constexpr std::string_view kCppLineCommentPrefix = "//";
	constexpr std::size_t kStartOfDocument = 0;

	Core::OpenItem ItemWith(std::string content)
	{
		Core::OpenItem item;

		Core::Result<Core::TextDocument> document = Core::TextDocument::FromContent(std::move(content), "test.cpp");
		REQUIRE(document);

		item.Document = std::make_shared<Core::TextDocument>(std::move(document).GetValue());
		Core::RecomputeLineCaches(item);

		return item;
	}

	std::string TextOf(const Core::OpenItem& item)
	{
		return item.Document->GetBuffer().Materialize();
	}

	void SelectFromTo(Core::OpenItem& item, std::size_t anchor, std::size_t offset)
	{
		item.Caret.Anchor = anchor;
		item.Caret.Offset = offset;
	}

	void PlaceCaretAt(Core::OpenItem& item, std::size_t offset)
	{
		SelectFromTo(item, offset, offset);
	}
}

TEST_CASE("The caret's line is commented, and the caret stays on the same character")
{
	Core::OpenItem item = ItemWith("int a;\nint b;");
	const std::size_t insideFirstInt = 2;
	PlaceCaretAt(item, insideFirstInt);

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "// int a;\nint b;");
	CHECK(item.Caret.Offset == 5);
	CHECK(item.Caret.Anchor == 5);
}

TEST_CASE("A commented line is uncommented together with the space after the prefix")
{
	Core::OpenItem item = ItemWith("    // int a;");
	PlaceCaretAt(item, kStartOfDocument);

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "    int a;");
}

TEST_CASE("A prefix with no space after it is removed alone")
{
	Core::OpenItem item = ItemWith("//int a;");

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "int a;");
}

TEST_CASE("A block is commented at its shallowest indentation and blank lines are skipped")
{
	Core::OpenItem item = ItemWith("    if (x)\n\n        y();\n    z();");
	item.Caret = Core::SelectAll(item.Document->GetBuffer());

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "    // if (x)\n\n    //     y();\n    // z();");
}

TEST_CASE("A partly commented block is commented everywhere")
{
	Core::OpenItem item = ItemWith("// a();\nb();");
	item.Caret = Core::SelectAll(item.Document->GetBuffer());

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "// // a();\n// b();");
}

TEST_CASE("A fully commented block is uncommented")
{
	Core::OpenItem item = ItemWith("// a();\n\n  //b();");
	item.Caret = Core::SelectAll(item.Document->GetBuffer());

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "a();\n\n  b();");
}

TEST_CASE("A selection ending at the start of a line leaves that line alone")
{
	Core::OpenItem item = ItemWith("a();\nb();");
	const std::size_t startOfSecondLine = 5;
	SelectFromTo(item, kStartOfDocument, startOfSecondLine);

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "// a();\nb();");
}

TEST_CASE("Tab and space indentation share no bytes so the prefix goes at column 0 rather than splitting a tab or mixing the two")
{
	Core::OpenItem item = ItemWith("\ta();\n    b();");
	item.Caret = Core::SelectAll(item.Document->GetBuffer());

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "// \ta();\n//     b();");
}

TEST_CASE("Toggling is one undo step that collapses the caret onto its oldest edit because the history keeps offsets rather than carets")
{
	Core::OpenItem item = ItemWith("a();\nb();");
	item.Caret = Core::SelectAll(item.Document->GetBuffer());

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));
	REQUIRE(Core::Undo(item));

	CHECK(TextOf(item) == "a();\nb();");

	const std::size_t startOfLastLineWhereTheOldestEditWas = 5;
	CHECK(item.Caret.Offset == startOfLastLineWhereTheOldestEditWas);
	CHECK(item.Caret.Anchor == startOfLastLineWhereTheOldestEditWas);
}

TEST_CASE("Undoing a comment on the caret's line puts the caret back on that line")
{
	Core::OpenItem item = ItemWith("int a;\nint b;");
	const std::size_t insideSecondInt = 9;
	PlaceCaretAt(item, insideSecondInt);

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));
	REQUIRE(Core::Undo(item));

	CHECK(TextOf(item) == "int a;\nint b;");
	CHECK(item.Caret.Offset == 7);
	CHECK(item.Caret.Anchor == 7);
}

TEST_CASE("A caret inside the leading whitespace stays where it was")
{
	Core::OpenItem item = ItemWith("    a();");
	const std::size_t insideLeadingWhitespace = 1;
	PlaceCaretAt(item, insideLeadingWhitespace);

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "    // a();");
	CHECK(item.Caret.Offset == 1);
	CHECK(item.Caret.Anchor == 1);

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "    a();");
	CHECK(item.Caret.Offset == 1);
}

TEST_CASE("A caret inside the removed prefix lands where the prefix began")
{
	Core::OpenItem item = ItemWith("    // a();");
	const std::size_t betweenTheTwoSlashes = 5;
	PlaceCaretAt(item, betweenTheTwoSlashes);

	REQUIRE(Core::ToggleLineComment(item, kCppLineCommentPrefix));

	CHECK(TextOf(item) == "    a();");
	CHECK(item.Caret.Offset == 4);
}

TEST_CASE("A block of blank lines and an empty prefix change nothing")
{
	Core::OpenItem blank = ItemWith("\n   \n");
	blank.Caret = Core::SelectAll(blank.Document->GetBuffer());
	CHECK_FALSE(Core::ToggleLineComment(blank, kCppLineCommentPrefix));

	Core::OpenItem plain = ItemWith("text");
	const std::string_view emptyPrefix = "";
	CHECK_FALSE(Core::ToggleLineComment(plain, emptyPrefix));
}
