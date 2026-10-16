#include <doctest/doctest.h>

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentEditing.h"
#include "Core/Text/TextDocument.h"

#include <string>

using namespace Core;

namespace {
	TextDocument DocumentWith(std::string content)
	{
		Result<TextDocument> document = TextDocument::FromContent(std::move(content), "test.cpp");
		REQUIRE(document);

		return std::move(document).GetValue();
	}

	Caret CaretAt(std::size_t offset)
	{
		Caret caret;
		caret.Offset = offset;
		caret.Anchor = offset;

		return caret;
	}
}

TEST_CASE("Typing at the document level moves only the caret it is given")
{
	TextDocument document = DocumentWith("abc");
	Caret typing = CaretAt(1);

	REQUIRE(InsertText(document, typing, "xy"));

	CHECK(document.GetBuffer().Materialize() == "axybc");
	CHECK(typing.Offset == 3);
}

TEST_CASE("Deleting backward at the document level removes the character before the caret")
{
	TextDocument document = DocumentWith("abc");
	Caret caret = CaretAt(3);

	REQUIRE(DeleteBackward(document, caret, 4));

	CHECK(document.GetBuffer().Materialize() == "ab");
	CHECK(caret.Offset == 2);
}

TEST_CASE("Undo at the document level puts the caret where the undone edit was")
{
	TextDocument document = DocumentWith("abc");
	Caret caret = CaretAt(3);
	REQUIRE(InsertText(document, caret, "\nnext"));

	REQUIRE(Undo(document, caret));

	CHECK(document.GetBuffer().Materialize() == "abc");
	CHECK(caret.Offset == 3);
	CHECK(caret.Anchor == 3);
}

TEST_CASE("A newline at the document level copies the line's indentation")
{
	TextDocument document = DocumentWith("    body");
	Caret caret = CaretAt(8);

	REQUIRE(InsertNewline(document, caret));

	CHECK(document.GetBuffer().Materialize() == "    body\n    ");
	CHECK(caret.Offset == 13);
}

TEST_CASE("Commenting at the document level comments the caret's line")
{
	TextDocument document = DocumentWith("int x;");
	Caret caret = CaretAt(0);

	REQUIRE(ToggleLineComment(document, caret, "//"));

	CHECK(document.GetBuffer().Materialize() == "// int x;");
}
