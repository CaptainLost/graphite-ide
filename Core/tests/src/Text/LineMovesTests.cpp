#include <doctest/doctest.h>

#include "Core/Text/Caret.h"
#include "Core/Text/DocumentEditing.h"
#include "Core/Text/TextDocument.h"

#include <string>

using namespace Core;

namespace {
	TextDocument LinesDocument(std::string content)
	{
		Result<TextDocument> document = TextDocument::FromContent(std::move(content), "test.cpp");
		REQUIRE(document);

		return std::move(document).GetValue();
	}

	Caret CaretOn(std::size_t offset)
	{
		Caret caret;
		caret.Offset = offset;
		caret.Anchor = offset;

		return caret;
	}
}

TEST_CASE("Moving a line down swaps it with the next and the caret goes with it")
{
	TextDocument document = LinesDocument("one\ntwo\nthree\n");
	Caret caret = CaretOn(1);

	REQUIRE(MoveLines(document, caret, 1));

	CHECK(document.GetBuffer().Materialize() == "two\none\nthree\n");
	CHECK(caret.Offset == 5);
}

TEST_CASE("Moving a line up swaps it with the previous and the caret goes with it")
{
	TextDocument document = LinesDocument("one\ntwo\nthree");
	Caret caret = CaretOn(10);

	REQUIRE(MoveLines(document, caret, -1));

	CHECK(document.GetBuffer().Materialize() == "one\nthree\ntwo");
	CHECK(caret.Offset == 6);
}

TEST_CASE("Moving the selected lines keeps them selected and one undo puts them back")
{
	TextDocument document = LinesDocument("a\nb\nc\nd");
	Caret caret;
	caret.Anchor = 2;
	caret.Offset = 5;

	REQUIRE(MoveLines(document, caret, 1));
	CHECK(document.GetBuffer().Materialize() == "a\nd\nb\nc");
	CHECK(caret.Anchor == 4);
	CHECK(caret.Offset == 7);

	REQUIRE(Undo(document, caret));
	CHECK(document.GetBuffer().Materialize() == "a\nb\nc\nd");
}

TEST_CASE("A line at the edge does not move past it")
{
	TextDocument document = LinesDocument("one\ntwo");
	Caret top = CaretOn(0);
	Caret bottom = CaretOn(5);

	CHECK_FALSE(MoveLines(document, top, -1));
	CHECK_FALSE(MoveLines(document, bottom, 1));
	CHECK(document.GetBuffer().Materialize() == "one\ntwo");
}

TEST_CASE("Duplicating a line puts the copy below and the caret on it")
{
	TextDocument document = LinesDocument("one\ntwo\n");
	Caret caret = CaretOn(1);

	REQUIRE(DuplicateLines(document, caret));

	CHECK(document.GetBuffer().Materialize() == "one\none\ntwo\n");
	CHECK(caret.Offset == 5);
}

TEST_CASE("Duplicating the last line with no newline after it adds one between")
{
	TextDocument document = LinesDocument("one\ntwo");
	Caret caret = CaretOn(6);

	REQUIRE(DuplicateLines(document, caret));

	CHECK(document.GetBuffer().Materialize() == "one\ntwo\ntwo");
	CHECK(caret.Offset == 10);
}
