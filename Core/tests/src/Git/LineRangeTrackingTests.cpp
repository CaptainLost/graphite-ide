#include <doctest/doctest.h>

#include "Core/Git/LineRangeTracking.h"

#include <optional>
#include <vector>

using Core::DiffLine;
using Core::DiffLineKind;
using Core::FileDiff;
using Core::Hunk;
using Core::OneBasedLineRange;

namespace {

	Hunk HunkWithoutLines(int oldStart, int oldCount, int newStart, int newCount)
	{
		Hunk hunk;
		hunk.OldStart = oldStart;
		hunk.OldCount = oldCount;
		hunk.NewStart = newStart;
		hunk.NewCount = newCount;
		return hunk;
	}

	Hunk HunkWithLines(int oldStart, int oldCount, int newStart, int newCount,
					   std::vector<DiffLine> lines)
	{
		Hunk hunk = HunkWithoutLines(oldStart, oldCount, newStart, newCount);
		hunk.Lines = std::move(lines);
		return hunk;
	}

	DiffLine ContextLine(int oldLine, int newLine)
	{
		DiffLine line;
		line.Kind = DiffLineKind::Context;
		line.OldLine = oldLine;
		line.NewLine = newLine;
		return line;
	}

	DiffLine AddedLine(int newLine)
	{
		DiffLine line;
		line.Kind = DiffLineKind::Added;
		line.NewLine = newLine;
		return line;
	}

	DiffLine RemovedLine(int oldLine)
	{
		DiffLine line;
		line.Kind = DiffLineKind::Removed;
		line.OldLine = oldLine;
		return line;
	}

	FileDiff MakeFile(std::vector<Hunk> hunks)
	{
		FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.Hunks = std::move(hunks);
		return file;
	}
}

TEST_CASE("A hunk entirely after the range leaves it untouched")
{
	const FileDiff file = MakeFile({HunkWithoutLines(50, 1, 50, 3)});

	CHECK_FALSE(Core::TouchesRange(file, OneBasedLineRange{.First = 10, .Last = 20}));

	const std::optional<OneBasedLineRange> mapped = Core::MapRangeToParent(file, OneBasedLineRange{.First = 10, .Last = 20});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 10);
	CHECK(mapped->Last == 20);
}

TEST_CASE("A hunk before the range shifts it by the lines it added")
{
	const FileDiff file = MakeFile({HunkWithoutLines(2, 2, 2, 5)});

	CHECK_FALSE(Core::TouchesRange(file, OneBasedLineRange{.First = 20, .Last = 30}));

	const std::optional<OneBasedLineRange> mapped = Core::MapRangeToParent(file, OneBasedLineRange{.First = 20, .Last = 30});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 17);
	CHECK(mapped->Last == 27);
}

TEST_CASE("A hunk before the range shifts it back by the lines it removed")
{
	const FileDiff file = MakeFile({HunkWithoutLines(2, 5, 2, 2)});

	const std::optional<OneBasedLineRange> mapped = Core::MapRangeToParent(file, OneBasedLineRange{.First = 20, .Last = 30});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 23);
	CHECK(mapped->Last == 33);
}

TEST_CASE("A hunk overlapping the range makes the commit interesting")
{
	const FileDiff file = MakeFile({HunkWithoutLines(12, 1, 12, 2)});

	CHECK(Core::TouchesRange(file, OneBasedLineRange{.First = 10, .Last = 20}));
}

TEST_CASE("An overlapping hunk without per-line detail shrinks Last by the lines it added, never "
		  "below its old side")
{
	const FileDiff file = MakeFile({HunkWithoutLines(12, 2, 12, 5)});

	const std::optional<OneBasedLineRange> mapped = Core::MapRangeToParent(file, OneBasedLineRange{.First = 10, .Last = 20});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 10);

	CHECK(mapped->Last == 17);
}

TEST_CASE("Several hunks before the range accumulate their shifts")
{
	const FileDiff file = MakeFile({HunkWithoutLines(2, 1, 2, 3), HunkWithoutLines(10, 4, 12, 1)});

	const std::optional<OneBasedLineRange> mapped = Core::MapRangeToParent(file, OneBasedLineRange{.First = 40, .Last = 45});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 41);
	CHECK(mapped->Last == 46);
}

TEST_CASE("A range in a file this commit created has no parent range")
{
	FileDiff file;
	file.Change = Core::FileChange::Added;

	CHECK(Core::TouchesRange(file, OneBasedLineRange{.First = 1, .Last = 5}));
	CHECK_FALSE(Core::MapRangeToParent(file, OneBasedLineRange{.First = 1, .Last = 5}).has_value());
}

TEST_CASE("Endpoints inside a hunk without per-line detail are left unshifted")
{
	const FileDiff file = MakeFile({HunkWithoutLines(1, 1, 1, 8)});

	const std::optional<OneBasedLineRange> mapped = Core::MapRangeToParent(file, OneBasedLineRange{.First = 2, .Last = 3});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 2);
	CHECK(mapped->Last == 3);
}

TEST_CASE("Replacing the middle of a range widens Last to every old line it consumed")
{
	const Hunk hunk = HunkWithLines(1, 5, 1, 3,
									{
										ContextLine(1, 1),
										RemovedLine(2),
										RemovedLine(3),
										RemovedLine(4),
										AddedLine(2),
										ContextLine(5, 3),
									});
	const FileDiff file = MakeFile({hunk});

	CHECK(Core::TouchesRange(file, OneBasedLineRange{.First = 2, .Last = 2}));

	const std::optional<OneBasedLineRange> mapped = Core::MapRangeToParent(file, OneBasedLineRange{.First = 2, .Last = 2});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 2);

	CHECK(mapped->Last == 4);
}

TEST_CASE("A removed line inside the range makes a line-aware hunk touch it")
{
	const Hunk hunk = HunkWithLines(1, 3, 1, 2,
									{
										ContextLine(1, 1),
										RemovedLine(2),
										ContextLine(3, 2),
									});
	const FileDiff file = MakeFile({hunk});

	CHECK(Core::TouchesRange(file, OneBasedLineRange{.First = 2, .Last = 2}));
}

TEST_CASE("A context line's position maps to its exact old line")
{
	const Hunk hunk = HunkWithLines(1, 1, 1, 4,
									{
										AddedLine(1),
										AddedLine(2),
										AddedLine(3),
										ContextLine(1, 4),
									});
	const FileDiff file = MakeFile({hunk});

	const std::optional<OneBasedLineRange> mapped = Core::MapRangeToParent(file, OneBasedLineRange{.First = 4, .Last = 4});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 1);
	CHECK(mapped->Last == 1);
}

TEST_CASE("A removal at the very start of a hunk is attributed to the position it "
		  "would have been read at")
{
	const Hunk hunk = HunkWithLines(5, 2, 5, 1,
									{
										RemovedLine(5),
										ContextLine(6, 5),
									});
	const FileDiff file = MakeFile({hunk});

	CHECK(Core::TouchesRange(file, OneBasedLineRange{.First = 5, .Last = 5}));
}

TEST_CASE("Trace line to parent finds an unchanged line through context")
{
	const FileDiff file = MakeFile({HunkWithLines(4, 1, 5, 1, {ContextLine(4, 5)})});

	const Core::TracedLine traced = Core::TraceLineToParent(file, 5);
	CHECK(traced.Fate == Core::LineFate::Unchanged);
	CHECK(traced.OldLine == 4);
}

TEST_CASE("Trace line to parent pairs removed and added lines by position")
{
	const FileDiff file = MakeFile({HunkWithLines(7, 2, 7, 3,
												  {
													  RemovedLine(7),
													  RemovedLine(8),
													  AddedLine(7),
													  AddedLine(8),
													  AddedLine(9),
												  })});

	const Core::TracedLine second = Core::TraceLineToParent(file, 8);
	CHECK(second.Fate == Core::LineFate::Changed);
	CHECK(second.OldLine == 8);
	CHECK(Core::TraceLineToParent(file, 9).Fate == Core::LineFate::Added);
}

TEST_CASE("Trace line to parent calls every line of an added file added")
{
	FileDiff file = MakeFile({HunkWithLines(0, 0, 1, 2, {AddedLine(1), AddedLine(2)})});
	file.Change = Core::FileChange::Added;

	CHECK(Core::TraceLineToParent(file, 1).Fate == Core::LineFate::Added);
	CHECK(Core::TraceLineToParent(file, 2).Fate == Core::LineFate::Added);
}

TEST_CASE("Trace line to parent shifts a line below a hunk")
{
	const FileDiff file = MakeFile({HunkWithLines(10, 0, 10, 2, {AddedLine(10), AddedLine(11)})});

	const Core::TracedLine traced = Core::TraceLineToParent(file, 20);
	CHECK(traced.Fate == Core::LineFate::Unchanged);
	CHECK(traced.OldLine == 18);
}
