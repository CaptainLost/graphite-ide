#include <doctest/doctest.h>

#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Editor/LensModel.h"

namespace {

	Graphite::Declaration Function(std::string name, std::size_t line)
	{
		return Graphite::Declaration{std::move(name), Graphite::DeclarationKind::Function, line,
									 Graphite::Utf16Column{0}};
	}
}

TEST_CASE("A declaration becomes a lens anchored on its line's first row")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 2)}, rows, buffer, 4);

	CHECK(model.GetRows().At(2).IsLens);
	REQUIRE(model.LensAt(2) != nullptr);
	CHECK_FALSE(model.LensAt(2)->CountKnown);
}

TEST_CASE("A lens with no count yet is asked about, and only once it is answered stops being")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1), Function("Update", 3)}, rows, buffer, 4);

	CHECK(model.RowsNeedingCounts(0, 4).size() == 2);

	model.SetCount(1, 12);

	CHECK(model.RowsNeedingCounts(0, 4) == std::vector<std::size_t>{3});
	CHECK(model.LensAt(1)->Count == 12);
	CHECK(model.LensAt(1)->CountKnown);
}

TEST_CASE("Only lenses in the asked-about range are reported")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\nf\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("A", 0), Function("B", 5)}, rows, buffer, 4);

	CHECK(model.RowsNeedingCounts(0, 2) == std::vector<std::size_t>{0});
}

TEST_CASE("A zero count is a known count, not a missing one")
{
	const Core::TextBuffer buffer{"a\nb\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Dead", 0)}, rows, buffer, 4);
	model.SetCount(0, 0);

	CHECK(model.LensAt(0)->CountKnown);
	CHECK(model.RowsNeedingCounts(0, 2).empty());
}

TEST_CASE("Inserting lines above a lens moves it and keeps its count")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 3)}, rows, buffer, 4);
	model.SetCount(3, 7);

	model.OnLinesChanged(1, 2, rows);

	REQUIRE(model.LensAt(5) != nullptr);
	CHECK(model.LensAt(5)->Count == 7);
}

TEST_CASE("An edit below a lens leaves it where it was")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1)}, rows, buffer, 4);

	model.OnLinesChanged(3, 4, rows);

	CHECK(model.LensAt(1) != nullptr);
}
