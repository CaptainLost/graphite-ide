#include <doctest/doctest.h>

#include "Core/Text/RowMap.h"
#include "Core/Text/TextBuffer.h"
#include "Editor/Overlays/LensModel.h"

namespace {

	Graphite::Declaration Function(std::string name, std::size_t line)
	{
		return Graphite::Declaration{std::move(name), Graphite::DeclarationKind::Function, line,
									 Graphite::Utf16Column{0}};
	}

	Graphite::Declaration FunctionNamedOn(std::string name, std::size_t line, std::size_t nameLine)
	{
		Graphite::Declaration declaration = Function(std::move(name), line);
		declaration.NameLine = nameLine;

		return declaration;
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

TEST_CASE("Inserting lines above a lens moves its name line with it")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({FunctionNamedOn("Draw", 3, 3)}, rows, buffer, 4);

	model.OnLinesChanged(1, 2, rows);

	REQUIRE(model.LensAt(5) != nullptr);
	CHECK(model.LensAt(5)->NameLine == 5);
}

TEST_CASE("Deleting lines above a lens moves its name line up with it")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\nf\ng\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({FunctionNamedOn("Draw", 5, 5)}, rows, buffer, 4);

	model.OnLinesChanged(1, -2, rows);

	REQUIRE(model.LensAt(3) != nullptr);
	CHECK(model.LensAt(3)->NameLine == 3);
}

TEST_CASE("A lens starting above an edit moves its name line when the name sits below the edit")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\nf\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({FunctionNamedOn("Draw", 1, 3)}, rows, buffer, 4);

	model.OnLinesChanged(2, 2, rows);

	REQUIRE(model.LensAt(1) != nullptr);
	CHECK(model.LensAt(1)->NameLine == 5);
}

TEST_CASE("A lens whose name line was deleted names the line before the deletion")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\nf\ng\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({FunctionNamedOn("Draw", 1, 3)}, rows, buffer, 4);

	model.OnLinesChanged(2, -3, rows);

	REQUIRE(model.LensAt(1) != nullptr);
	CHECK(model.LensAt(1)->NameLine == 1);
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

	model.SetDeclarations({Function("A", 1), Function("B", 1)}, rows, buffer, 4);

	REQUIRE(model.LensAt(1) != nullptr);
	CHECK(model.LensAt(1)->CountKnown);
	CHECK(model.LensAt(1)->Count == 5);
}

TEST_CASE("A declaration on a wrapped line anchors on the line's first row, not the line number")
{
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

TEST_CASE("A lens asked about is not offered again until the asking is forgotten")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1)}, rows, buffer, 4);

	CHECK(model.TakeRowsToAsk(0, 4) == std::vector<std::size_t>{1});
	CHECK(model.TakeRowsToAsk(0, 4).empty());
	CHECK(model.RowsNeedingCounts(0, 4) == std::vector<std::size_t>{1});

	model.ForgetAsked();

	CHECK(model.TakeRowsToAsk(0, 4) == std::vector<std::size_t>{1});
}

TEST_CASE("A symbols answer after a save keeps the lens reference counts")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\nf\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1), Function("Update", 3)}, rows, buffer, 4);
	REQUIRE(model.TakeRowsToAsk(0, 6) == std::vector<std::size_t>{1, 3});
	model.SetCount(1, 12);

	model.SetDeclarations({Function("Draw", 1), Function("Update", 3), Function("Added", 5)}, rows, buffer, 4);

	REQUIRE(model.LensAt(1) != nullptr);
	CHECK(model.LensAt(1)->CountKnown);
	CHECK(model.LensAt(1)->Count == 12);
	CHECK(model.TakeRowsToAsk(0, 6) == std::vector<std::size_t>{5});
}

TEST_CASE("Overloads keep their own counts across a symbols answer")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1), Function("Draw", 3)}, rows, buffer, 4);
	model.SetCount(1, 5);
	model.SetCount(3, 7);

	model.SetDeclarations({Function("Draw", 0), Function("Draw", 1), Function("Draw", 3)}, rows, buffer, 4);

	CHECK(model.LensAt(1)->Count == 5);
	CHECK(model.LensAt(3)->Count == 7);
	CHECK_FALSE(model.LensAt(0)->CountKnown);
}

TEST_CASE("Overloads that all moved to other rows keep their counts in order")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\ne\nf\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1), Function("Draw", 3)}, rows, buffer, 4);
	model.SetCount(1, 5);
	model.SetCount(3, 7);

	model.SetDeclarations({Function("Draw", 2), Function("Draw", 5)}, rows, buffer, 4);

	CHECK(model.LensAt(2)->Count == 5);
	CHECK(model.LensAt(5)->Count == 7);
}

TEST_CASE("A lens whose declaration moved to another row is asked about again, as its question named the old row")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1)}, rows, buffer, 4);
	REQUIRE(model.TakeRowsToAsk(0, 4) == std::vector<std::size_t>{1});

	model.SetDeclarations({Function("Draw", 2)}, rows, buffer, 4);

	CHECK(model.TakeRowsToAsk(0, 4) == std::vector<std::size_t>{2});
}

TEST_CASE("A lens whose count could not be had says so and is asked again only once the asking is forgotten")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1)}, rows, buffer, 4);
	REQUIRE(model.TakeRowsToAsk(0, 4) == std::vector<std::size_t>{1});

	model.SetCountFailed(1);

	REQUIRE(model.LensAt(1) != nullptr);
	CHECK(model.LensAt(1)->CountFailed);
	CHECK_FALSE(model.LensAt(1)->CountKnown);
	CHECK(model.TakeRowsToAsk(0, 4).empty());

	model.ForgetAsked();

	CHECK_FALSE(model.LensAt(1)->CountFailed);
	CHECK(model.TakeRowsToAsk(0, 4) == std::vector<std::size_t>{1});
}

TEST_CASE("A count that arrives after a failure is known and no longer failed")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1)}, rows, buffer, 4);
	model.SetCountFailed(1);

	model.SetCount(1, 5);

	CHECK(model.LensAt(1)->CountKnown);
	CHECK_FALSE(model.LensAt(1)->CountFailed);
}

TEST_CASE("Forgetting the failed counts offers them again and leaves the counts still asked alone")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1), Function("Update", 3)}, rows, buffer, 4);
	REQUIRE(model.TakeRowsToAsk(0, 4).size() == 2);

	model.SetCountFailed(1);
	model.ForgetFailed();

	CHECK_FALSE(model.LensAt(1)->CountFailed);
	CHECK(model.TakeRowsToAsk(0, 4) == std::vector<std::size_t>{1});
}

TEST_CASE("A remembered count is shown at once and still asked of the server")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SeedCounts({{"Draw", Graphite::DeclarationKind::Function, 7}});
	model.SetDeclarations({Function("Draw", 1)}, rows, buffer, 4);

	CHECK(model.TakeRowsToAsk(0, 4) == std::vector<std::size_t>{1});
	CHECK(model.LensAt(1)->Count == 7);

	model.SetCount(1, 3);
	CHECK(model.LensAt(1)->Count == 3);
	CHECK(model.TakeRowsToAsk(0, 4).empty());
}

TEST_CASE("Counts marked stale are asked again and keep showing until answered")
{
	const Core::TextBuffer buffer{"a\nb\nc\nd\n"};
	Core::RowMap rows;
	rows.Rebuild(buffer, Core::RowMap::kNoWrap, 4);

	Graphite::LensModel model;
	model.SetDeclarations({Function("Draw", 1), Function("Update", 3)}, rows, buffer, 4);
	REQUIRE(model.TakeRowsToAsk(0, 4).size() == 2);
	model.SetCount(1, 0);
	model.SetCount(3, 2);
	REQUIRE(model.TakeRowsToAsk(0, 4).empty());

	model.MarkCountsStale();

	CHECK(model.TakeRowsToAsk(0, 4) == std::vector<std::size_t>{1, 3});
	CHECK(model.LensAt(1)->CountKnown);
	CHECK(model.LensAt(1)->Count == 0);

	model.SetCount(1, 3);
	model.SetCountFailed(3);

	CHECK(model.LensAt(1)->Count == 3);
	CHECK(model.LensAt(3)->Count == 2);
	CHECK_FALSE(model.LensAt(3)->CountFailed);
	CHECK(model.TakeRowsToAsk(0, 4).empty());
}
