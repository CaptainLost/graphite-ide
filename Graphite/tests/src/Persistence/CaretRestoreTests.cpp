#include <doctest/doctest.h>

#include "Persistence/CaretRestore.h"

#include "Core/Text/TextBuffer.h"

using namespace Graphite;

TEST_CASE("A caret on a line that is still there lands where it was")
{
	const Core::TextBuffer buffer{"first\nsecond\nthird"};

	CHECK(RestoredCaretOffset(buffer, 1, 3) == 9);
}

TEST_CASE("A caret past the end of a line that got shorter lands at the line's end")
{
	const Core::TextBuffer buffer{"first\nab\nthird"};

	CHECK(RestoredCaretOffset(buffer, 1, 40) == 8);
}

TEST_CASE("A caret on a line the file no longer has lands at the end of the file")
{
	const Core::TextBuffer buffer{"first\nsecond"};

	CHECK(RestoredCaretOffset(buffer, 200, 5) == buffer.GetLength());
}

TEST_CASE("A caret inside a multi-byte character lands at its start")
{
	const Core::TextBuffer buffer{"za\xC5\xBC\xC3\xB3\xC5\x82\xC4\x87"};

	CHECK(RestoredCaretOffset(buffer, 0, 3) == 2);
	CHECK(RestoredCaretOffset(buffer, 0, 5) == 4);
	CHECK(RestoredCaretOffset(buffer, 0, 4) == 4);
}

TEST_CASE("A top line the file no longer has becomes its last line")
{
	const Core::TextBuffer buffer{"a\nb\nc"};

	CHECK(RestoredTopLine(buffer, 1) == 1);
	CHECK(RestoredTopLine(buffer, 90) == 2);
}

TEST_CASE("An empty file restores to its start")
{
	const Core::TextBuffer buffer;

	CHECK(RestoredCaretOffset(buffer, 5, 5) == 0);
	CHECK(RestoredTopLine(buffer, 5) == 0);
}
