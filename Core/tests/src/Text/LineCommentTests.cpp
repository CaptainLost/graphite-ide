#include "Core/Text/Editing.h"

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextDocument.h"

#include <doctest/doctest.h>

#include <string>

namespace {
	Core::OpenItem ItemWith(std::string content)
	{
		Core::OpenItem item;

		auto document = Core::TextDocument::FromContent(std::move(content), "test.cpp");
		REQUIRE(document);

		item.Document = std::move(document).GetValue();
		Core::RefreshDerived(item);

		return item;
	}

	std::string TextOf(const Core::OpenItem& item)
	{
		const Core::TextBuffer& buffer = item.Document->GetBuffer();
		return buffer.Substring(0, buffer.GetLength());
	}

	void Select(Core::OpenItem& item, std::size_t anchor, std::size_t offset)
	{
		item.Caret.Anchor = anchor;
		item.Caret.Offset = offset;
	}
}

TEST_CASE("The caret's line is commented, and the caret stays on the same character")
{
	Core::OpenItem item = ItemWith("int a;\nint b;");
	Select(item, 2, 2);

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "// int a;\nint b;");
	CHECK(item.Caret.Offset == 5);
	CHECK(item.Caret.Anchor == 5);
}

TEST_CASE("A commented line is uncommented together with the space after the prefix")
{
	Core::OpenItem item = ItemWith("    // int a;");
	Select(item, 0, 0);

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "    int a;");
}

TEST_CASE("A prefix with no space after it is removed alone")
{
	Core::OpenItem item = ItemWith("//int a;");

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "int a;");
}

TEST_CASE("A block is commented at its shallowest indentation and blank lines are skipped")
{
	Core::OpenItem item = ItemWith("    if (x)\n\n        y();\n    z();");
	Select(item, 0, item.Document->GetBuffer().GetLength());

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "    // if (x)\n\n    //     y();\n    // z();");
}

TEST_CASE("A partly commented block is commented everywhere")
{
	Core::OpenItem item = ItemWith("// a();\nb();");
	Select(item, 0, item.Document->GetBuffer().GetLength());

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "// // a();\n// b();");
}

TEST_CASE("A fully commented block is uncommented")
{
	Core::OpenItem item = ItemWith("// a();\n\n  //b();");
	Select(item, 0, item.Document->GetBuffer().GetLength());

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "a();\n\n  b();");
}

TEST_CASE("A selection ending at the start of a line leaves that line alone")
{
	Core::OpenItem item = ItemWith("a();\nb();");
	Select(item, 0, 5);

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "// a();\nb();");
}

// The prefix goes in at a column the lines share byte for byte. A line indented with a tab and
// one indented with spaces share no indentation at all, so the prefix goes at column 0 rather
// than splitting a tab or mixing the two.
TEST_CASE("Mixed tab and space indentation is commented at the shortest prefix")
{
	Core::OpenItem item = ItemWith("\ta();\n    b();");
	Select(item, 0, item.Document->GetBuffer().GetLength());

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "// \ta();\n//     b();");
}

TEST_CASE("Toggling is one undo step")
{
	Core::OpenItem item = ItemWith("a();\nb();");
	Select(item, 0, item.Document->GetBuffer().GetLength());

	REQUIRE(Core::ToggleLineComment(item, "//"));
	REQUIRE(Core::Undo(item));

	CHECK(TextOf(item) == "a();\nb();");

	// The history keeps offsets, not carets, so undo cannot give a selection back. It collapses
	// the caret onto the oldest edit of the step: the comment taken off the last line.
	CHECK(item.Caret.Offset == 5);
	CHECK(item.Caret.Anchor == 5);
}

TEST_CASE("Undoing a comment on the caret's line puts the caret back on that line")
{
	Core::OpenItem item = ItemWith("int a;\nint b;");
	Select(item, 9, 9);

	REQUIRE(Core::ToggleLineComment(item, "//"));
	REQUIRE(Core::Undo(item));

	CHECK(TextOf(item) == "int a;\nint b;");
	CHECK(item.Caret.Offset == 7);
	CHECK(item.Caret.Anchor == 7);
}

TEST_CASE("A caret inside the leading whitespace stays where it was")
{
	Core::OpenItem item = ItemWith("    a();");
	Select(item, 1, 1);

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "    // a();");
	CHECK(item.Caret.Offset == 1);
	CHECK(item.Caret.Anchor == 1);

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "    a();");
	CHECK(item.Caret.Offset == 1);
}

TEST_CASE("A caret inside the removed prefix lands where the prefix began")
{
	Core::OpenItem item = ItemWith("    // a();");
	Select(item, 5, 5);

	REQUIRE(Core::ToggleLineComment(item, "//"));

	CHECK(TextOf(item) == "    a();");
	CHECK(item.Caret.Offset == 4);
}

TEST_CASE("A block of blank lines and an empty prefix change nothing")
{
	Core::OpenItem blank = ItemWith("\n   \n");
	Select(blank, 0, blank.Document->GetBuffer().GetLength());
	CHECK_FALSE(Core::ToggleLineComment(blank, "//"));

	Core::OpenItem plain = ItemWith("text");
	CHECK_FALSE(Core::ToggleLineComment(plain, ""));
}
