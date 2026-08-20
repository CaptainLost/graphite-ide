#include <doctest/doctest.h>

#include "Core/Git/LineRangeTracking.h"

using Core::FileDiff;
using Core::Hunk;
using Core::LineRange;

namespace {

	// A hunk that replaced `oldCount` lines starting at `oldStart` with `newCount` lines
	// starting at `newStart`. The line contents never matter to this arithmetic.
	Hunk MakeHunk(int oldStart, int oldCount, int newStart, int newCount)
	{
		Hunk hunk;
		hunk.OldStart = oldStart;
		hunk.OldCount = oldCount;
		hunk.NewStart = newStart;
		hunk.NewCount = newCount;
		return hunk;
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
	const FileDiff file = MakeFile({MakeHunk(50, 1, 50, 3)});

	CHECK_FALSE(Core::TouchesRange(file, LineRange{10, 20}));

	const auto mapped = Core::MapRangeToParent(file, LineRange{10, 20});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 10);
	CHECK(mapped->Last == 20);
}

TEST_CASE("A hunk before the range shifts it by the lines it added")
{
	// Two lines became five, so everything below moved down by three.
	const FileDiff file = MakeFile({MakeHunk(2, 2, 2, 5)});

	CHECK_FALSE(Core::TouchesRange(file, LineRange{20, 30}));

	const auto mapped = Core::MapRangeToParent(file, LineRange{20, 30});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 17);
	CHECK(mapped->Last == 27);
}

TEST_CASE("A hunk before the range shifts it back by the lines it removed")
{
	const FileDiff file = MakeFile({MakeHunk(2, 5, 2, 2)});

	const auto mapped = Core::MapRangeToParent(file, LineRange{20, 30});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 23);
	CHECK(mapped->Last == 33);
}

TEST_CASE("A hunk overlapping the range makes the commit interesting")
{
	const FileDiff file = MakeFile({MakeHunk(12, 1, 12, 2)});

	CHECK(Core::TouchesRange(file, LineRange{10, 20}));
}

TEST_CASE("An overlapping hunk widens the range to cover its old side")
{
	// Lines 12-13 became 12-16. Looking one commit back, the lines that produced this
	// range are 12-13 plus whatever else the range covered.
	const FileDiff file = MakeFile({MakeHunk(12, 2, 12, 5)});

	const auto mapped = Core::MapRangeToParent(file, LineRange{10, 20});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 10);

	// The range shrinks by the three lines the hunk added, but never below the end of
	// the hunk's own old side.
	CHECK(mapped->Last == 17);
}

TEST_CASE("Several hunks before the range accumulate their shifts")
{
	const FileDiff file = MakeFile({MakeHunk(2, 1, 2, 3), MakeHunk(10, 4, 12, 1)});

	const auto mapped = Core::MapRangeToParent(file, LineRange{40, 45});

	// +2 from the first hunk, -3 from the second, so the parent's lines are one higher.
	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 41);
	CHECK(mapped->Last == 46);
}

TEST_CASE("A range in a file this commit created has no parent range")
{
	FileDiff file;
	file.Change = Core::FileChange::Added;

	CHECK(Core::TouchesRange(file, LineRange{1, 5}));
	CHECK_FALSE(Core::MapRangeToParent(file, LineRange{1, 5}).has_value());
}

TEST_CASE("A range never falls below the first line")
{
	const FileDiff file = MakeFile({MakeHunk(1, 1, 1, 8)});

	const auto mapped = Core::MapRangeToParent(file, LineRange{2, 3});

	// A shift computed blindly would land on line -4 and index out of the parent's text.
	REQUIRE(mapped.has_value());
	CHECK(mapped->First >= 1);
	CHECK(mapped->Last >= mapped->First);
}
