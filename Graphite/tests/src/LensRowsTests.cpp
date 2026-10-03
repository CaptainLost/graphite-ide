#include <doctest/doctest.h>

#include "Editor/Overlays/LensRows.h"

namespace {

	Graphite::LensRows Rows(std::vector<std::size_t> anchors)
	{
		Graphite::LensRows rows;
		rows.SetAnchors(std::move(anchors));
		return rows;
	}
}

TEST_CASE("With no anchors a screen row is its document row")
{
	Graphite::LensRows rows = Rows({});

	CHECK(rows.TotalScreenRows(100) == 100);
	CHECK_FALSE(rows.At(7).IsLens);
	CHECK(rows.At(7).DocumentRow == 7);
	CHECK(rows.ScreenRowOf(7) == 7);
}

TEST_CASE("Each anchor adds one screen row")
{
	Graphite::LensRows rows = Rows({3, 10});

	CHECK(rows.TotalScreenRows(100) == 102);
}

TEST_CASE("The lens sits immediately above the row it anchors to")
{
	Graphite::LensRows rows = Rows({3});

	CHECK_FALSE(rows.At(2).IsLens);
	CHECK(rows.At(2).DocumentRow == 2);

	CHECK(rows.At(3).IsLens);
	CHECK(rows.At(3).DocumentRow == 3);

	CHECK_FALSE(rows.At(4).IsLens);
	CHECK(rows.At(4).DocumentRow == 3);
}

TEST_CASE("A document row maps to the screen row that draws it, not to its lens")
{
	Graphite::LensRows rows = Rows({3});

	CHECK(rows.ScreenRowOf(2) == 2);
	CHECK(rows.ScreenRowOf(3) == 4);
	CHECK(rows.ScreenRowOf(4) == 5);
}

TEST_CASE("An anchor on the first row still gets its lens above it")
{
	Graphite::LensRows rows = Rows({0});

	CHECK(rows.At(0).IsLens);
	CHECK(rows.At(1).DocumentRow == 0);
	CHECK(rows.ScreenRowOf(0) == 1);
}

TEST_CASE("Adjacent anchors each keep their own lens")
{
	Graphite::LensRows rows = Rows({5, 6});

	CHECK(rows.At(5).IsLens);
	CHECK(rows.At(6).DocumentRow == 5);
	CHECK(rows.At(7).IsLens);
	CHECK(rows.At(8).DocumentRow == 6);
	CHECK(rows.ScreenRowOf(6) == 8);
}

TEST_CASE("At and ScreenRowOf are inverses on every non-lens row")
{
	Graphite::LensRows rows = Rows({0, 1, 4, 9});

	for (std::size_t documentRow = 0; documentRow < 12; ++documentRow)
	{
		CHECK(rows.At(rows.ScreenRowOf(documentRow)).DocumentRow == documentRow);
		CHECK_FALSE(rows.At(rows.ScreenRowOf(documentRow)).IsLens);
	}
}

TEST_CASE("An edit that inserts lines moves only the anchors below it")
{
	Graphite::LensRows rows = Rows({2, 8});

	rows.ShiftAnchors(3, 5);

	CHECK(rows.At(2).IsLens);
	CHECK(rows.At(12).IsLens);
}

TEST_CASE("An edit that removes lines drops an anchor it would push above the edit")
{
	Graphite::LensRows rows = Rows({2, 6});

	rows.ShiftAnchors(-4, 5);

	CHECK(rows.TotalScreenRows(20) == 21);
	CHECK(rows.At(2).IsLens);
}

TEST_CASE("Duplicate anchors collapse to one lens")
{
	Graphite::LensRows rows = Rows({3, 3});

	CHECK(rows.TotalScreenRows(10) == 11);
	CHECK(rows.At(3).IsLens);
	CHECK(rows.ScreenRowOf(3) == 4);
}

TEST_CASE("The last screen row in range is the last document row, not a lens")
{
	Graphite::LensRows rows = Rows({0, 4});

	const std::size_t documentRowCount = 10;
	const Graphite::LensRows::Row last = rows.At(rows.TotalScreenRows(documentRowCount) - 1);

	CHECK_FALSE(last.IsLens);
	CHECK(last.DocumentRow == documentRowCount - 1);
}
