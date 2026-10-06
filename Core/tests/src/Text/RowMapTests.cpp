#include <doctest/doctest.h>

#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Core/Text/WrapLayout.h"

#include <string>
#include <vector>

using namespace Core;

namespace {
	constexpr std::size_t kTabWidth = 4;
	constexpr std::size_t kWideEnoughNotToWrap = 80;
	constexpr std::size_t kFourColumnsPerRow = 4;

	TextBuffer BufferOf(const std::string& text)
	{
		return TextBuffer{text};
	}
}

TEST_CASE("Without wrapping every line is one row")
{
	const TextBuffer buffer = BufferOf("aaa\nbbb\nccc");

	RowMap rows;
	rows.Rebuild(buffer, kWideEnoughNotToWrap, kTabWidth);

	CHECK(rows.GetTotalRows() == 3);
	CHECK(rows.FirstRowOf(0) == 0);
	CHECK(rows.FirstRowOf(2) == 2);
	CHECK(rows.RowAt(1).Line == 1);
	CHECK(rows.RowAt(1).SubRow == 0);
}

TEST_CASE("A wrapped line occupies as many rows as it has breaks plus one")
{
	const TextBuffer buffer = BufferOf("aaaaaaaaaa\nbb");

	RowMap rows;
	rows.Rebuild(buffer, kFourColumnsPerRow, kTabWidth);

	const std::vector<std::size_t> breaksAtColumnsFourAndEight{4, 8};
	CHECK(rows.BreaksOf(0) == breaksAtColumnsFourAndEight);
	CHECK(rows.RowsOf(0) == 3);
	CHECK(rows.RowsOf(1) == 1);
	CHECK(rows.GetTotalRows() == 4);
}

TEST_CASE("A row resolves to its line and its position within it")
{
	const TextBuffer buffer = BufferOf("aaaaaaaaaa\nbb");

	RowMap rows;
	rows.Rebuild(buffer, kFourColumnsPerRow, kTabWidth);

	CHECK(rows.RowAt(0).Line == 0);
	CHECK(rows.RowAt(0).SubRow == 0);
	CHECK(rows.RowAt(2).Line == 0);
	CHECK(rows.RowAt(2).SubRow == 2);
	CHECK(rows.RowAt(3).Line == 1);
	CHECK(rows.RowAt(3).SubRow == 0);
}

TEST_CASE("A row past the end resolves to the last row of the last line")
{
	const TextBuffer buffer = BufferOf("bb\naaaaaaaaaa");

	RowMap rows;
	rows.Rebuild(buffer, kFourColumnsPerRow, kTabWidth);
	REQUIRE(rows.GetTotalRows() == 4);

	const std::size_t rowPastTheEnd = 100;

	CHECK(rows.RowAt(rowPastTheEnd).Line == 1);
	CHECK(rows.RowAt(rowPastTheEnd).SubRow == 2);
	CHECK(rows.RowAt(rows.GetTotalRows()).Line == 1);
	CHECK(rows.RowAt(rows.GetTotalRows()).SubRow == 2);
}

TEST_CASE("FirstRowOf and RowAt agree in both directions")
{
	const TextBuffer buffer = BufferOf("aaaaaaaaaa\nbb\ncccccc\nd");

	RowMap rows;
	rows.Rebuild(buffer, kFourColumnsPerRow, kTabWidth);

	for (std::size_t line = 0; line < buffer.GetLineCount(); ++line)
	{
		const std::size_t row = rows.FirstRowOf(line);

		CHECK(rows.RowAt(row).Line == line);
		CHECK(rows.RowAt(row).SubRow == 0);
	}
}

TEST_CASE("Updating one line moves every row after it")
{
	TextBuffer buffer = BufferOf("aa\nbb\ncc");

	RowMap rows;
	rows.Rebuild(buffer, kFourColumnsPerRow, kTabWidth);
	CHECK(rows.GetTotalRows() == 3);
	CHECK(rows.FirstRowOf(2) == 2);

	buffer.Erase(3, 5);
	buffer.Insert(3, "bbbbbbbbbb");
	rows.UpdateLine(buffer, 1);

	CHECK(rows.RowsOf(1) == 3);
	CHECK(rows.GetTotalRows() == 5);
	CHECK(rows.FirstRowOf(2) == 4);
}

TEST_CASE("kNoWrap gives every line exactly one row where a width of zero would wrap at every column")
{
	const TextBuffer buffer = BufferOf("aaaaaaaaaaaaaaaaaaaa\nbb");

	RowMap rows;
	rows.Rebuild(buffer, RowMap::kNoWrap, kTabWidth);

	CHECK(rows.GetTotalRows() == 2);
	CHECK(rows.BreaksOf(0).empty());

	const std::size_t zeroColumnsPerRow = 0;
	RowMap zeroWidthRows;
	zeroWidthRows.Rebuild(buffer, zeroColumnsPerRow, kTabWidth);

	CHECK(zeroWidthRows.RowsOf(0) == 20);
}

TEST_CASE("A line of zero rows is skipped, which is what folding will need")
{
	const TextBuffer buffer = BufferOf("aa\nbb\ncc");

	RowMap rows;
	rows.Rebuild(buffer, kWideEnoughNotToWrap, kTabWidth);
	rows.SetRowsOf(1, RowMap::kNoRows);

	CHECK(rows.GetTotalRows() == 2);
	CHECK(rows.RowAt(0).Line == 0);
	CHECK(rows.RowAt(1).Line == 2);
}

TEST_CASE("Two adjacent zero-row lines of a folded block are both stepped over by the descent")
{
	const TextBuffer buffer = BufferOf("aa\nbb\ncc\ndd");

	RowMap rows;
	rows.Rebuild(buffer, kWideEnoughNotToWrap, kTabWidth);
	rows.SetRowsOf(1, RowMap::kNoRows);
	rows.SetRowsOf(2, RowMap::kNoRows);

	CHECK(rows.GetTotalRows() == 2);
	CHECK(rows.RowAt(0).Line == 0);
	CHECK(rows.RowAt(1).Line == 3);
	CHECK(rows.FirstRowOf(3) == 1);
}

TEST_CASE("A zero-row first line does not claim row zero although the descent starts there")
{
	const TextBuffer buffer = BufferOf("aa\nbb");

	RowMap rows;
	rows.Rebuild(buffer, kWideEnoughNotToWrap, kTabWidth);
	rows.SetRowsOf(0, RowMap::kNoRows);

	CHECK(rows.GetTotalRows() == 1);
	CHECK(rows.RowAt(0).Line == 1);
	CHECK(rows.RowAt(0).SubRow == 0);
	CHECK(rows.FirstRowOf(1) == 0);
}

TEST_CASE("A caret on a visible line maps correctly around a zero-row line")
{
	// A caret inside a zero-row line has no defined position, so only the visible lines
	// either side of it are pinned.
	const TextBuffer buffer = BufferOf("aa\nbb\ncc");

	RowMap rows;
	rows.Rebuild(buffer, kWideEnoughNotToWrap, kTabWidth);
	rows.SetRowsOf(1, RowMap::kNoRows);

	const std::size_t secondCharacterOfFirstLine = 1;
	const std::size_t secondCharacterOfThirdLine = 7;

	const VisualPosition before = CaretToVisual(buffer, rows, secondCharacterOfFirstLine, kTabWidth);

	CHECK(before.Row == 0);
	CHECK(before.Column == 1);

	const VisualPosition after = CaretToVisual(buffer, rows, secondCharacterOfThirdLine, kTabWidth);

	CHECK(after.Row == 1);
	CHECK(after.Column == 1);

	CHECK(VisualToCaret(buffer, rows, before, kTabWidth) == secondCharacterOfFirstLine);
	CHECK(VisualToCaret(buffer, rows, after, kTabWidth) == secondCharacterOfThirdLine);
}
