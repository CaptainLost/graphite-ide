#include <doctest/doctest.h>

#include "Editor/Overlays/LensRows.h"
#include "Editor/Overlays/ScreenRows.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace {

	using RowKind = Graphite::ScreenRows::RowKind;

	constexpr std::size_t kPeekRows = 9;

	Graphite::LensRows Lenses(std::vector<std::size_t> anchors)
	{
		Graphite::LensRows lenses;
		lenses.SetAnchors(std::move(anchors));
		return lenses;
	}
}

TEST_CASE("Screen rows without a peek answer as the lens rows do")
{
	Graphite::LensRows lenses;
	lenses.SetAnchors({2, 5});
	const Graphite::ScreenRows rows{lenses, std::nullopt};
	for (std::size_t screen = 0; screen < 12; ++screen)
	{
		CHECK(rows.At(screen).DocumentRow == lenses.At(screen).DocumentRow);
		CHECK((rows.At(screen).Kind == RowKind::Lens) == lenses.At(screen).IsLens);
	}
	CHECK(rows.ScreenRowOf(5) == lenses.ScreenRowOf(5));
	CHECK(rows.TotalScreenRows(10) == lenses.TotalScreenRows(10));
	CHECK_FALSE(rows.PeekTop().has_value());
}

TEST_CASE("Screen rows put a peek between two lines")
{
	const Graphite::LensRows lenses;
	const Graphite::ScreenRows rows{lenses, Graphite::PeekPlacement{.BeforeDocumentRow = 5, .Rows = kPeekRows}};
	CHECK(rows.At(4).Kind == RowKind::Document);
	CHECK(rows.At(5).Kind == RowKind::Peek);
	CHECK(rows.At(13).PeekRow == 8);
	CHECK(rows.At(14).DocumentRow == 5);
	CHECK(rows.ScreenRowOf(5) == 14);
	CHECK(rows.TotalScreenRows(10) == 19);
	CHECK(rows.PeekTop() == std::optional<std::size_t>{5});
}

TEST_CASE("Screen rows put a peek above the lens of the line below it")
{
	const Graphite::LensRows lenses = Lenses({5});
	const Graphite::ScreenRows rows{lenses, Graphite::PeekPlacement{.BeforeDocumentRow = 5, .Rows = kPeekRows}};

	for (std::size_t screen = 5; screen <= 13; ++screen)
	{
		CHECK(rows.At(screen).Kind == RowKind::Peek);
		CHECK(rows.At(screen).PeekRow == screen - 5);
	}

	CHECK(rows.At(14).Kind == RowKind::Lens);
	CHECK(rows.At(14).DocumentRow == 5);
	CHECK(rows.At(15).Kind == RowKind::Document);
	CHECK(rows.At(15).DocumentRow == 5);
	CHECK(rows.ScreenRowOf(5) == 15);
	CHECK(rows.PeekTop() == std::optional<std::size_t>{5});
}

TEST_CASE("Screen rows count a lens above the peek")
{
	const Graphite::LensRows lenses = Lenses({2});
	const Graphite::ScreenRows rows{lenses, Graphite::PeekPlacement{.BeforeDocumentRow = 5, .Rows = kPeekRows}};

	CHECK(rows.At(2).Kind == RowKind::Lens);
	CHECK(rows.At(2).DocumentRow == 2);
	CHECK(rows.At(5).Kind == RowKind::Document);
	CHECK(rows.At(5).DocumentRow == 4);
	CHECK(rows.At(6).Kind == RowKind::Peek);
	CHECK(rows.At(6).PeekRow == 0);
	CHECK(rows.At(15).Kind == RowKind::Document);
	CHECK(rows.At(15).DocumentRow == 5);
	CHECK(rows.PeekTop() == std::optional<std::size_t>{6});
}

TEST_CASE("Screen rows place a peek under the last line")
{
	const std::size_t documentRowCount = 10;
	const Graphite::LensRows lenses;
	const Graphite::ScreenRows rows{lenses,
									Graphite::PeekPlacement{.BeforeDocumentRow = documentRowCount, .Rows = kPeekRows}};

	CHECK(rows.At(9).Kind == RowKind::Document);
	CHECK(rows.At(9).DocumentRow == 9);

	for (std::size_t screen = 10; screen <= 18; ++screen)
	{
		CHECK(rows.At(screen).Kind == RowKind::Peek);
	}

	CHECK(rows.At(19).Kind == RowKind::Document);
	CHECK(rows.At(19).DocumentRow == documentRowCount);
	CHECK(rows.TotalScreenRows(documentRowCount) == 19);
}

TEST_CASE("Screen rows agree with themselves on every row around lenses and a peek")
{
	const std::size_t documentRowCount = 12;
	const Graphite::LensRows lenses = Lenses({0, 2, 5, 6, 11});

	for (std::size_t before = 0; before <= documentRowCount; ++before)
	{
		CAPTURE(before);

		const Graphite::ScreenRows rows{lenses, Graphite::PeekPlacement{.BeforeDocumentRow = before, .Rows = kPeekRows}};
		const std::size_t peekTop = rows.PeekTop().value_or(0);

		for (std::size_t documentRow = 0; documentRow < documentRowCount; ++documentRow)
		{
			CAPTURE(documentRow);

			const Graphite::ScreenRows::Row row = rows.At(rows.ScreenRowOf(documentRow));

			CHECK(row.Kind == RowKind::Document);
			CHECK(row.DocumentRow == documentRow);
		}

		std::size_t documentRows = 0;
		std::size_t lensRows = 0;
		std::size_t peekRows = 0;
		std::size_t previousDocumentRow = 0;

		for (std::size_t screen = 0; screen < rows.TotalScreenRows(documentRowCount); ++screen)
		{
			CAPTURE(screen);

			const Graphite::ScreenRows::Row row = rows.At(screen);
			const bool insidePeek = screen >= peekTop && screen < peekTop + kPeekRows;

			CHECK((row.Kind == RowKind::Peek) == insidePeek);
			CHECK(row.DocumentRow >= previousDocumentRow);

			previousDocumentRow = row.DocumentRow;

			switch (row.Kind)
			{
			case RowKind::Document:
				CHECK(rows.ScreenRowOf(row.DocumentRow) == screen);
				++documentRows;
				break;
			case RowKind::Lens:
				CHECK(rows.ScreenRowOf(row.DocumentRow) == screen + 1);
				++lensRows;
				break;
			case RowKind::Peek:
				CHECK(row.DocumentRow == before);
				CHECK(row.PeekRow == screen - peekTop);
				++peekRows;
				break;
			}
		}

		CHECK(documentRows == documentRowCount);
		CHECK(lensRows == 5);
		CHECK(peekRows == kPeekRows);
		CHECK(rows.At(rows.TotalScreenRows(documentRowCount)).Kind == RowKind::Document);
		CHECK(rows.At(rows.TotalScreenRows(documentRowCount)).DocumentRow == documentRowCount);
	}
}
