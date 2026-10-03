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

TEST_CASE("Two declarations on one anchor row: the first one wins")
{
	const Core::TextBuffer buffer{"a\nb\nc\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("A", 1)}, rows, buffer, 4);
	model.SetCount(1, 5);

	// "A" is listed first again, so it should be the survivor and keep its count; if "B" won
	// instead, its identity would not match the previous carry-over map and the count would be
	// lost.
	model.SetDeclarations({Function("A", 1), Function("B", 1)}, rows, buffer, 4);

	REQUIRE(model.LensAt(1) != nullptr);
	CHECK(model.LensAt(1)->CountKnown);
	CHECK(model.LensAt(1)->Count == 5);
}

TEST_CASE("A declaration on a wrapped line anchors on the line's first row, not the line number")
{
	// Columns per row of 1 forces every non-empty line to wrap onto several rows, so a
	// declaration's line number and its first row diverge - this is the reason anchors are rows
	// (FirstRowOf) rather than lines in the first place.
	const Core::TextBuffer buffer{"ab\ncd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, 1, 4);

	REQUIRE(rows.FirstRowOf(1) != 1);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1)}, rows, buffer, 4);

	REQUIRE(model.LensAt(rows.FirstRowOf(1)) != nullptr);
	CHECK(model.GetRows().At(rows.FirstRowOf(1)).IsLens);
}

TEST_CASE("A declaration naming a line the buffer no longer has gets no lens")
{
	// A reply describes the text the server last saw. The buffer here has three lines, and the
	// declaration names a fourth - a lens anchored on it would be drawn past the end of the
	// file. Filtered inside SetDeclarations, so a rebuild after a rewrap drops it too, not only
	// the frame the reply arrived on.
	const Core::TextBuffer buffer{"a\nb\nc\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1), Function("Gone", 9)}, rows, buffer, 4);

	CHECK(model.LensAt(1) != nullptr);
	CHECK(model.RowsNeedingCounts(0, rows.GetTotalRows()) == std::vector<std::size_t>{1});
}

TEST_CASE("Only answered lenses are worth remembering")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1), Function("Update", 3)}, rows, buffer, 4);
	model.SetCount(1, 7);

	const std::vector<Graphite::KnownCount> remembered = model.KnownCounts();

	REQUIRE(remembered.size() == 1);
	CHECK(remembered[0].Name == "Draw");
	CHECK(remembered[0].Kind == Graphite::DeclarationKind::Function);
	CHECK(remembered[0].Count == 7);
}

TEST_CASE("A count remembered from before is known the moment its declaration arrives")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SeedCounts({{"Draw", Graphite::DeclarationKind::Function, 7}});
	model.SetDeclarations({Function("Draw", 1)}, rows, buffer, 4);

	REQUIRE(model.LensAt(1) != nullptr);
	CHECK(model.LensAt(1)->CountKnown);
	CHECK(model.LensAt(1)->Count == 7);
	CHECK(model.RowsNeedingCounts(0, 4).empty());
}

TEST_CASE("A remembered count whose declaration is gone is left behind")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SeedCounts({{"Deleted", Graphite::DeclarationKind::Function, 7}});
	model.SetDeclarations({Function("Draw", 1)}, rows, buffer, 4);

	CHECK_FALSE(model.LensAt(1)->CountKnown);
	CHECK(model.KnownCounts().empty());
}
