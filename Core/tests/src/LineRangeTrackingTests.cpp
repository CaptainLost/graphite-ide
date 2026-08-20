#include <doctest/doctest.h>

#include "Core/Git/LineRangeTracking.h"

using Core::DiffLine;
using Core::DiffLineKind;
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

	// The same, but with the per-line detail a real diff carries and a hand-built hunk
	// normally leaves empty. Tests that need to exercise the `Lines`-aware paths — as
	// opposed to the hunk-level fallback every other test in this file uses — build one
	// of these instead.
	Hunk MakeHunk(int oldStart, int oldCount, int newStart, int newCount,
				  std::vector<DiffLine> lines)
	{
		Hunk hunk = MakeHunk(oldStart, oldCount, newStart, newCount);
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

	// The hunk reaches as far as both endpoints, so neither accumulates a shift — a
	// shift only ever comes from a hunk entirely above the position being mapped. With
	// no per-line detail to resolve them exactly, the fallback leaves both endpoints
	// exactly where they were rather than guessing where a shorter parent would put them.
	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 2);
	CHECK(mapped->Last == 3);
}

TEST_CASE("Replacing the middle of a range widens Last to the old lines it consumed")
{
	// A five-line file has its middle three lines collapsed into one: "a,b,c,d,e"
	// becomes "a,Z,e". The single hunk spans the whole file on both sides.
	const Hunk hunk = MakeHunk(1, 5, 1, 3,
	{
		ContextLine(1, 1),
		RemovedLine(2),
		RemovedLine(3),
		RemovedLine(4),
		AddedLine(2),
		ContextLine(5, 3),
	});
	const FileDiff file = MakeFile({hunk});

	CHECK(Core::TouchesRange(file, LineRange{2, 2}));

	const auto mapped = Core::MapRangeToParent(file, LineRange{2, 2});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 2);

	// The mapped position alone only reaches old line 2 (no shift applies inside the
	// hunk), but this commit actually consumed old lines 2, 3 and 4 to produce the one
	// line asked about — an earlier commit that wrote line 3 or 4 must still be visible.
	CHECK(mapped->Last == 4);
}

TEST_CASE("A removed line inside the range makes a line-aware hunk touch it")
{
	// "a,b,c" loses its middle line and becomes "a,c". The hunk spans the whole file,
	// so a hunk-level check alone could not tell this apart from a context-only hunk —
	// only the Removed entry says line 2 was actually edited.
	const Hunk hunk = MakeHunk(1, 3, 1, 2,
	{
		ContextLine(1, 1),
		RemovedLine(2),
		ContextLine(3, 2),
	});
	const FileDiff file = MakeFile({hunk});

	CHECK(Core::TouchesRange(file, LineRange{2, 2}));
}

TEST_CASE("A context line's position maps to its exact old line")
{
	// Three lines are inserted ahead of a single surviving line: "target" becomes
	// "header,header,header,target". The surviving line is context within the same
	// hunk, so only its own Old/NewLine pair — not a hunk-wide shift — gives its
	// parent-side position.
	const Hunk hunk = MakeHunk(1, 1, 1, 4,
	{
		AddedLine(1),
		AddedLine(2),
		AddedLine(3),
		ContextLine(1, 4),
	});
	const FileDiff file = MakeFile({hunk});

	const auto mapped = Core::MapRangeToParent(file, LineRange{4, 4});

	REQUIRE(mapped.has_value());
	CHECK(mapped->First == 1);
	CHECK(mapped->Last == 1);
}

TEST_CASE("A removal at the very start of a hunk is attributed to the position it "
		  "would have been read at")
{
	// The hunk's first line entry is itself a removal, with no preceding context line
	// inside the hunk to anchor it — the position has to come from the hunk's own
	// NewStart, not from whatever the previous line entry happened to leave behind.
	const Hunk hunk = MakeHunk(5, 2, 5, 1,
	{
		RemovedLine(5),
		ContextLine(6, 5),
	});
	const FileDiff file = MakeFile({hunk});

	CHECK(Core::TouchesRange(file, LineRange{5, 5}));
}
