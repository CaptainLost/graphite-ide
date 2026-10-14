#include <doctest/doctest.h>

#include "Git/Diff/DiffRows.h"

#include "GitFixtures.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using Graphite::DiffPair;
using Graphite::DiffRow;
using Graphite::DiffRowKind;

namespace {

	Core::Hunk ExpandingHunk(int oldStart, int newStart)
	{
		Core::Hunk hunk;
		hunk.OldStart = oldStart;
		hunk.OldCount = 1;
		hunk.NewStart = newStart;
		hunk.NewCount = 2;
		hunk.Header = "@@ hunk @@\n";
		hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Removed, oldStart, 0, "old\n"));
		hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Added, 0, newStart, "new-1\n"));
		hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Added, 0, newStart + 1, "new-2\n"));
		return hunk;
	}
}

TEST_CASE("A file with no hunks produces no rows")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;

	CHECK(Graphite::BuildUnifiedRows(file).empty());
	CHECK(Graphite::BuildSideBySideRows(file).empty());
}

TEST_CASE("The unified view opens each hunk with its header row")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 2);
	CHECK(rows[0].Kind == DiffRowKind::HunkHeader);
	CHECK(rows[1].Kind == DiffRowKind::Context);
}

TEST_CASE("The unified view keeps the diff's own order")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n"),
											  MakeDiffLine(Core::DiffLineKind::Removed, 2, 0, "two\n"),
											  MakeDiffLine(Core::DiffLineKind::Added, 0, 2, "TWO\n"),
											  MakeDiffLine(Core::DiffLineKind::Context, 3, 3, "three\n")});

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 5);
	CHECK(rows[1].Kind == DiffRowKind::Context);
	CHECK(rows[2].Kind == DiffRowKind::Removed);
	CHECK(rows[3].Kind == DiffRowKind::Added);
	CHECK(rows[4].Kind == DiffRowKind::Context);
}

TEST_CASE("A row can find its own line back in the diff")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Added, 0, 1, "new\n")});

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 2);

	const Core::DiffLine* line = Graphite::DiffLineOf(file, rows[1]);

	REQUIRE(line != nullptr);
	CHECK(line->Text == "new\n");

	CHECK(Graphite::DiffLineOf(file, rows[0]) == nullptr);
}

TEST_CASE("DiffLineOf returns null when a row's hunk index no longer fits the file")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});

	DiffRow row;
	row.Kind = DiffRowKind::Context;
	row.Hunk = 1;
	row.Line = 0;

	CHECK(Graphite::DiffLineOf(file, row) == nullptr);
}

TEST_CASE("DiffLineOf returns null when a row's line index no longer fits its hunk")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});

	DiffRow row;
	row.Kind = DiffRowKind::Context;
	row.Hunk = 0;
	row.Line = 1;

	CHECK(Graphite::DiffLineOf(file, row) == nullptr);
}

TEST_CASE("Side by side puts a context line on both halves")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 2);
	REQUIRE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Left->OldLine == 1);
	CHECK(pairs[1].Right->NewLine == 1);
}

TEST_CASE("Side by side pairs a removed line with the added line that replaced it")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Removed, 2, 0, "two\n"),
											  MakeDiffLine(Core::DiffLineKind::Added, 0, 2, "TWO\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 2);
	REQUIRE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Left->Kind == DiffRowKind::Removed);
	CHECK(pairs[1].Right->Kind == DiffRowKind::Added);
	CHECK(pairs[1].Left->OldLine == 2);
	CHECK(pairs[1].Right->NewLine == 2);
}

TEST_CASE("Side by side leaves the other half empty when a line was only deleted")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Removed, 2, 0, "two\n"),
											  MakeDiffLine(Core::DiffLineKind::Removed, 3, 0, "three\n"),
											  MakeDiffLine(Core::DiffLineKind::Added, 0, 2, "TWO\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 3);

	CHECK(pairs[1].Left.has_value());
	CHECK(pairs[1].Right.has_value());

	REQUIRE(pairs[2].Left.has_value());
	CHECK(pairs[2].Left->OldLine == 3);
	CHECK_FALSE(pairs[2].Right.has_value());
}

TEST_CASE("Side by side leaves the left half empty for a pure insertion")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Added, 0, 1, "new\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 2);
	CHECK_FALSE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Right->NewLine == 1);
}

TEST_CASE("A hunk header spans both halves")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE_FALSE(pairs.empty());
	REQUIRE(pairs[0].Left.has_value());
	CHECK(pairs[0].Left->Kind == DiffRowKind::HunkHeader);

	REQUIRE(pairs[0].Right.has_value());
	CHECK(pairs[0].Right->Kind == DiffRowKind::HunkHeader);
}

TEST_CASE("Rows from a second hunk name their own hunk")
{
	Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});

	Core::Hunk second;
	second.OldStart = 20;
	second.NewStart = 20;
	second.Header = "@@ -20 +20 @@\n";
	second.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Added, 0, 20, "twenty\n"));
	file.Hunks.push_back(std::move(second));

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 5);
	CHECK(rows[0].Hunk == 0);
	CHECK(rows[1].Hunk == 0);
	CHECK(rows[2].Hunk == 1);
	CHECK(rows[3].Hunk == 1);
	CHECK(rows[4].Hunk == 1);

	const Core::DiffLine* line = Graphite::DiffLineOf(file, rows[4]);
	REQUIRE(line != nullptr);
	CHECK(line->Text == "twenty\n");
}

TEST_CASE("Side by side rows from a second hunk name their own hunk")
{
	Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});

	Core::Hunk second;
	second.OldStart = 20;
	second.NewStart = 20;
	second.Header = "@@ -20 +20 @@\n";
	second.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Added, 0, 20, "twenty\n"));
	file.Hunks.push_back(std::move(second));

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 5);
	CHECK(pairs[0].Left->Hunk == 0);
	CHECK(pairs[1].Left->Hunk == 0);
	CHECK(pairs[2].Left->Hunk == 1);
	CHECK(pairs[3].Left->Hunk == 1);

	REQUIRE(pairs[4].Right.has_value());
	CHECK(pairs[4].Right->Hunk == 1);
	CHECK_FALSE(pairs[4].Left.has_value());
}

TEST_CASE("Side by side pairs an added line with an empty left half when the hunk opens on an insertion")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Added, 0, 1, "new\n"),
											  MakeDiffLine(Core::DiffLineKind::Context, 1, 2, "one\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 3);
	CHECK_FALSE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Right->Kind == DiffRowKind::Added);
	CHECK(pairs[1].Right->NewLine == 1);
}

TEST_CASE("Side by side pairs alternating removed and added lines in order, not by kind")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Removed, 1, 0, "one\n"),
											  MakeDiffLine(Core::DiffLineKind::Added, 0, 1, "ONE\n"),
											  MakeDiffLine(Core::DiffLineKind::Removed, 2, 0, "two\n"),
											  MakeDiffLine(Core::DiffLineKind::Added, 0, 2, "TWO\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 3);

	REQUIRE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Left->OldLine == 1);
	CHECK(pairs[1].Right->NewLine == 1);

	REQUIRE(pairs[2].Left.has_value());
	REQUIRE(pairs[2].Right.has_value());
	CHECK(pairs[2].Left->OldLine == 2);
	CHECK(pairs[2].Right->NewLine == 2);
}

TEST_CASE("A whole file with no hunks is entirely Unchanged")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 3, 3);

	REQUIRE(rows.Unified.size() == 3);
	REQUIRE(rows.SideBySide.size() == 3);

	for (std::size_t rowIndex = 0; rowIndex < 3; ++rowIndex)
	{
		CAPTURE(rowIndex);
		CHECK(rows.Unified[rowIndex].Kind == DiffRowKind::Unchanged);
		CHECK(rows.Unified[rowIndex].OldLine == static_cast<int>(rowIndex) + 1);
		CHECK(rows.Unified[rowIndex].NewLine == static_cast<int>(rowIndex) + 1);

		REQUIRE(rows.SideBySide[rowIndex].Left.has_value());
		REQUIRE(rows.SideBySide[rowIndex].Right.has_value());
		CHECK(rows.SideBySide[rowIndex].Left->OldLine == static_cast<int>(rowIndex) + 1);
		CHECK(rows.SideBySide[rowIndex].Right->NewLine == static_cast<int>(rowIndex) + 1);
	}
}

TEST_CASE("A hunk in the middle leaves Unchanged rows before and after it, resuming at the "
		  "right number on each side once the hunk has shifted them apart")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 6, 7);

	REQUIRE(rows.Unified.size() == 8);

	CHECK(rows.Unified[0].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[0].OldLine == 1);
	CHECK(rows.Unified[0].NewLine == 1);
	CHECK(rows.Unified[1].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[1].OldLine == 2);
	CHECK(rows.Unified[1].NewLine == 2);

	CHECK(rows.Unified[2].Kind == DiffRowKind::Removed);
	CHECK(rows.Unified[3].Kind == DiffRowKind::Added);
	CHECK(rows.Unified[4].Kind == DiffRowKind::Added);

	CHECK(rows.Unified[5].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[5].OldLine == 4);
	CHECK(rows.Unified[5].NewLine == 5);
	CHECK(rows.Unified[6].OldLine == 5);
	CHECK(rows.Unified[6].NewLine == 6);
	CHECK(rows.Unified[7].OldLine == 6);
	CHECK(rows.Unified[7].NewLine == 7);
}

TEST_CASE("Side by side keeps an Unchanged row's two halves in step before a hunk and drifts "
		  "them apart after it")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 6, 7);

	REQUIRE(rows.SideBySide.size() == 7);

	REQUIRE(rows.SideBySide[0].Left.has_value());
	REQUIRE(rows.SideBySide[0].Right.has_value());
	CHECK(rows.SideBySide[0].Left->OldLine == 1);
	CHECK(rows.SideBySide[0].Right->NewLine == 1);

	REQUIRE(rows.SideBySide[4].Left.has_value());
	REQUIRE(rows.SideBySide[4].Right.has_value());
	CHECK(rows.SideBySide[4].Left->OldLine == 4);
	CHECK(rows.SideBySide[4].Right->NewLine == 5);
}

TEST_CASE("A hunk at the very first line leaves no Unchanged row ahead of it")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(1, 1));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 4, 5);

	REQUIRE(rows.Unified.size() == 3 + 3);
	CHECK(rows.Unified[0].Kind == DiffRowKind::Removed);

	CHECK(rows.Unified[3].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[3].OldLine == 2);
	CHECK(rows.Unified[3].NewLine == 3);
}

TEST_CASE("A hunk at the very last line leaves no Unchanged row after it")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 3, 4);

	REQUIRE(rows.Unified.size() == 2 + 3);
	CHECK(rows.Unified.back().Kind == DiffRowKind::Added);
	CHECK(rows.Unified.back().NewLine == 4);
}

TEST_CASE("Two hunks with a gap between them get Unchanged rows in the gap")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(2, 2));
	file.Hunks.push_back(ExpandingHunk(10, 11));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 12, 14);

	REQUIRE(rows.Unified.size() == 1 + 3 + 7 + 3 + 2);

	constexpr std::size_t leadingGapLineCount = 1;
	constexpr std::size_t firstHunkLineCount = 3;
	std::size_t rowIndex = leadingGapLineCount + firstHunkLineCount;

	for (int oldLine = 3; oldLine <= 9; ++oldLine, ++rowIndex)
	{
		CAPTURE(oldLine);
		CHECK(rows.Unified[rowIndex].Kind == DiffRowKind::Unchanged);
		CHECK(rows.Unified[rowIndex].OldLine == oldLine);
		CHECK(rows.Unified[rowIndex].NewLine == oldLine + 1);
	}

	CHECK(rows.Unified[rowIndex].Kind == DiffRowKind::Removed);
	CHECK(rows.Unified[rowIndex].Hunk == 1);
}

TEST_CASE("An added file has no old side in its whole-file rows")
{
	Core::Hunk hunk;
	hunk.OldStart = 0;
	hunk.OldCount = 0;
	hunk.NewStart = 1;
	hunk.NewCount = 2;
	hunk.Header = "@@ -0,0 +1,2 @@\n";
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Added, 0, 1, "one\n"));
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Added, 0, 2, "two\n"));

	Core::FileDiff file;
	file.Change = Core::FileChange::Added;
	file.NewPath = "new.txt";
	file.Hunks.push_back(std::move(hunk));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 0, 2);

	REQUIRE(rows.Unified.size() == 2);
	CHECK(rows.Unified[0].OldLine == 0);
	CHECK(rows.Unified[1].OldLine == 0);

	REQUIRE(rows.SideBySide.size() == 2);
	CHECK_FALSE(rows.SideBySide[0].Left.has_value());
	CHECK_FALSE(rows.SideBySide[1].Left.has_value());
	REQUIRE(rows.SideBySide[0].Right.has_value());
	REQUIRE(rows.SideBySide[1].Right.has_value());
}

TEST_CASE("A deleted file has no new side in its whole-file rows")
{
	Core::Hunk hunk;
	hunk.OldStart = 1;
	hunk.OldCount = 2;
	hunk.NewStart = 0;
	hunk.NewCount = 0;
	hunk.Header = "@@ -1,2 +0,0 @@\n";
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Removed, 1, 0, "one\n"));
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Removed, 2, 0, "two\n"));

	Core::FileDiff file;
	file.Change = Core::FileChange::Deleted;
	file.OldPath = "old.txt";
	file.Hunks.push_back(std::move(hunk));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 2, 0);

	REQUIRE(rows.Unified.size() == 2);
	CHECK(rows.Unified[0].NewLine == 0);
	CHECK(rows.Unified[1].NewLine == 0);

	REQUIRE(rows.SideBySide.size() == 2);
	CHECK_FALSE(rows.SideBySide[0].Right.has_value());
	CHECK_FALSE(rows.SideBySide[1].Right.has_value());
	REQUIRE(rows.SideBySide[0].Left.has_value());
	REQUIRE(rows.SideBySide[1].Left.has_value());
}

TEST_CASE("DiffLineOf returns null for an Unchanged row, which names no hunk to look a line up in")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});

	DiffRow row;
	row.Kind = DiffRowKind::Unchanged;
	row.OldLine = 5;
	row.NewLine = 5;

	CHECK(Graphite::DiffLineOf(file, row) == nullptr);
}

TEST_CASE("A pure insertion inside a two-sided file takes its gap length from the new side's count and resumes both sides at the right numbers")
{
	Core::Hunk hunk;
	hunk.OldStart = 3;
	hunk.OldCount = 0;
	hunk.NewStart = 4;
	hunk.NewCount = 2;
	hunk.Header = "@@ -3,0 +4,2 @@\n";
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Added, 0, 4, "ins-1\n"));
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Added, 0, 5, "ins-2\n"));

	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.NewPath = "a.txt";
	file.Hunks.push_back(std::move(hunk));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 6, 8);

	REQUIRE(rows.Unified.size() == 8);

	CHECK(rows.Unified[0].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[0].OldLine == 1);
	CHECK(rows.Unified[0].NewLine == 1);
	CHECK(rows.Unified[1].OldLine == 2);
	CHECK(rows.Unified[1].NewLine == 2);
	CHECK(rows.Unified[2].OldLine == 3);
	CHECK(rows.Unified[2].NewLine == 3);

	CHECK(rows.Unified[3].Kind == DiffRowKind::Added);
	CHECK(rows.Unified[3].NewLine == 4);
	CHECK(rows.Unified[4].Kind == DiffRowKind::Added);
	CHECK(rows.Unified[4].NewLine == 5);

	CHECK(rows.Unified[5].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[5].OldLine == 4);
	CHECK(rows.Unified[5].NewLine == 6);
	CHECK(rows.Unified[6].OldLine == 5);
	CHECK(rows.Unified[6].NewLine == 7);
	CHECK(rows.Unified[7].OldLine == 6);
	CHECK(rows.Unified[7].NewLine == 8);

	REQUIRE(rows.SideBySide.size() == 8);
	CHECK_FALSE(rows.SideBySide[3].Left.has_value());
	REQUIRE(rows.SideBySide[3].Right.has_value());
	CHECK(rows.SideBySide[3].Right->NewLine == 4);
	CHECK_FALSE(rows.SideBySide[4].Left.has_value());
	REQUIRE(rows.SideBySide[4].Right.has_value());
	CHECK(rows.SideBySide[4].Right->NewLine == 5);

	REQUIRE(rows.SideBySide[5].Left.has_value());
	REQUIRE(rows.SideBySide[5].Right.has_value());
	CHECK(rows.SideBySide[5].Left->OldLine == 4);
	CHECK(rows.SideBySide[5].Right->NewLine == 6);
}

TEST_CASE("A pure deletion inside a two-sided file takes its gap length from the old side's count and resumes both sides at the right numbers")
{
	Core::Hunk hunk;
	hunk.OldStart = 4;
	hunk.OldCount = 2;
	hunk.NewStart = 3;
	hunk.NewCount = 0;
	hunk.Header = "@@ -4,2 +3,0 @@\n";
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Removed, 4, 0, "del-1\n"));
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Removed, 5, 0, "del-2\n"));

	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.NewPath = "a.txt";
	file.Hunks.push_back(std::move(hunk));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 8, 6);

	REQUIRE(rows.Unified.size() == 8);

	CHECK(rows.Unified[0].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[0].OldLine == 1);
	CHECK(rows.Unified[0].NewLine == 1);
	CHECK(rows.Unified[1].OldLine == 2);
	CHECK(rows.Unified[1].NewLine == 2);
	CHECK(rows.Unified[2].OldLine == 3);
	CHECK(rows.Unified[2].NewLine == 3);

	CHECK(rows.Unified[3].Kind == DiffRowKind::Removed);
	CHECK(rows.Unified[3].OldLine == 4);
	CHECK(rows.Unified[4].Kind == DiffRowKind::Removed);
	CHECK(rows.Unified[4].OldLine == 5);

	CHECK(rows.Unified[5].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[5].OldLine == 6);
	CHECK(rows.Unified[5].NewLine == 4);
	CHECK(rows.Unified[6].OldLine == 7);
	CHECK(rows.Unified[6].NewLine == 5);
	CHECK(rows.Unified[7].OldLine == 8);
	CHECK(rows.Unified[7].NewLine == 6);

	REQUIRE(rows.SideBySide.size() == 8);
	REQUIRE(rows.SideBySide[3].Left.has_value());
	CHECK_FALSE(rows.SideBySide[3].Right.has_value());
	CHECK(rows.SideBySide[3].Left->OldLine == 4);
	REQUIRE(rows.SideBySide[4].Left.has_value());
	CHECK_FALSE(rows.SideBySide[4].Right.has_value());
	CHECK(rows.SideBySide[4].Left->OldLine == 5);

	REQUIRE(rows.SideBySide[5].Left.has_value());
	REQUIRE(rows.SideBySide[5].Right.has_value());
	CHECK(rows.SideBySide[5].Left->OldLine == 6);
	CHECK(rows.SideBySide[5].Right->NewLine == 4);
}

TEST_CASE("A line count smaller than the hunk's own numbers clamps the tail gap to zero instead of wrapping around")
{
	Core::Hunk hunk;
	hunk.OldStart = 1;
	hunk.OldCount = 3;
	hunk.NewStart = 1;
	hunk.NewCount = 3;
	hunk.Header = "@@ -1,3 +1,3 @@\n";
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n"));
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Context, 2, 2, "two\n"));
	hunk.Lines.push_back(MakeDiffLine(Core::DiffLineKind::Context, 3, 3, "three\n"));

	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.NewPath = "a.txt";
	file.Hunks.push_back(std::move(hunk));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 1, 1);

	REQUIRE(rows.Unified.size() == 3);
	CHECK(rows.Unified[0].Kind == DiffRowKind::Context);
	CHECK(rows.Unified[0].OldLine == 1);
	CHECK(rows.Unified[0].NewLine == 1);
	CHECK(rows.Unified[1].OldLine == 2);
	CHECK(rows.Unified[1].NewLine == 2);
	CHECK(rows.Unified[2].OldLine == 3);
	CHECK(rows.Unified[2].NewLine == 3);

	REQUIRE(rows.SideBySide.size() == 3);
	CHECK(rows.SideBySide[2].Left->OldLine == 3);
	CHECK(rows.SideBySide[2].Right->NewLine == 3);
}

namespace {

	DiffRow UnchangedRow(int oldLine, int newLine)
	{
		DiffRow row;
		row.Kind = DiffRowKind::Unchanged;
		row.OldLine = oldLine;
		row.NewLine = newLine;
		return row;
	}

	std::vector<Graphite::Token> OneToken(Graphite::TokenKind kind, std::size_t length)
	{
		constexpr std::size_t lineStart = 0;
		return {Graphite::Token{kind, lineStart, length}};
	}
}

TEST_CASE("An Unchanged row past the end of both sides resolves to nothing")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});
	const std::vector<std::string_view> lines{"one"};

	const Graphite::ResolvedLine resolved = Graphite::ResolveRow(
		file, UnchangedRow(9, 9), Graphite::DiffSide::New, lines, lines, Graphite::FileTokens{});

	CHECK_FALSE(resolved.Text.has_value());
	CHECK(resolved.Tokens == nullptr);
}

TEST_CASE("An Unchanged row resolves against the side it is drawn for, so each half of a rename uses its own lexer")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "same\n")});
	const std::vector<std::string_view> oldLines{"old"};
	const std::vector<std::string_view> newLines{"new"};

	Graphite::FileTokens tokens;
	tokens.Old.push_back(OneToken(Graphite::TokenKind::Comment, 3));
	tokens.New.push_back(OneToken(Graphite::TokenKind::Keyword, 3));

	const Graphite::ResolvedLine left = Graphite::ResolveRow(
		file, UnchangedRow(1, 1), Graphite::DiffSide::Old, oldLines, newLines, tokens);
	const Graphite::ResolvedLine right = Graphite::ResolveRow(
		file, UnchangedRow(1, 1), Graphite::DiffSide::New, oldLines, newLines, tokens);

	REQUIRE(left.Text.has_value());
	CHECK(*left.Text == "old");
	REQUIRE(left.Tokens != nullptr);
	CHECK(left.Tokens->front().Kind == Graphite::TokenKind::Comment);

	REQUIRE(right.Text.has_value());
	CHECK(*right.Text == "new");
	REQUIRE(right.Tokens != nullptr);
	CHECK(right.Tokens->front().Kind == Graphite::TokenKind::Keyword);
}

TEST_CASE("An Unchanged row falls back to the other side when its own has no line")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Removed, 1, 0, "gone\n")});
	const std::vector<std::string_view> oldLines{"only-old"};

	const Graphite::ResolvedLine resolved =
		Graphite::ResolveRow(file, UnchangedRow(1, 0), Graphite::DiffSide::New, oldLines,
							 std::vector<std::string_view>{}, Graphite::FileTokens{});

	REQUIRE(resolved.Text.has_value());
	CHECK(*resolved.Text == "only-old");
}

TEST_CASE("A token vector shorter than the file leaves the later lines uncoloured")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});
	const std::vector<std::string_view> lines{"one", "two"};

	Graphite::FileTokens tokens;
	tokens.New.push_back(OneToken(Graphite::TokenKind::Keyword, 3));

	const Graphite::ResolvedLine first = Graphite::ResolveRow(
		file, UnchangedRow(1, 1), Graphite::DiffSide::New, lines, lines, tokens);
	const Graphite::ResolvedLine second = Graphite::ResolveRow(
		file, UnchangedRow(2, 2), Graphite::DiffSide::New, lines, lines, tokens);

	CHECK(first.Tokens != nullptr);
	REQUIRE(second.Text.has_value());
	CHECK(*second.Text == "two");
	CHECK(second.Tokens == nullptr);
}

TEST_CASE("A changed row draws its own text without the line terminator")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Added, 0, 1, "added\r\n")});

	DiffRow row;
	row.Kind = DiffRowKind::Added;
	row.NewLine = 1;
	row.Hunk = 0;
	row.Line = 0;

	const Graphite::ResolvedLine resolved =
		Graphite::ResolveRow(file, row, Graphite::DiffSide::New, std::vector<std::string_view>{},
							 std::vector<std::string_view>{}, Graphite::FileTokens{});

	REQUIRE(resolved.Text.has_value());
	CHECK(*resolved.Text == "added");
}

TEST_CASE("A changed row whose indices no longer fit resolves to nothing")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Added, 0, 1, "added\n")});

	DiffRow row;
	row.Kind = DiffRowKind::Added;
	row.NewLine = 1;
	row.Hunk = 0;
	row.Line = 7;

	const Graphite::ResolvedLine resolved =
		Graphite::ResolveRow(file, row, Graphite::DiffSide::New, std::vector<std::string_view>{},
							 std::vector<std::string_view>{}, Graphite::FileTokens{});

	CHECK_FALSE(resolved.Text.has_value());
}

TEST_CASE("A row stands for its new-side line, a deleted line for its old one, a header for none")
{
	const Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Removed, 4, 0, "gone\n"),
											  MakeDiffLine(Core::DiffLineKind::Added, 0, 4, "come\n"),
											  MakeDiffLine(Core::DiffLineKind::Context, 5, 6, "kept\n")});

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);
	REQUIRE(rows.size() == 4);

	CHECK_FALSE(Graphite::AnchorOf(rows[0]).has_value());
	CHECK(Graphite::AnchorOf(rows[1]) == Graphite::DiffLineAnchor{Graphite::DiffSide::Old, 4});
	CHECK(Graphite::AnchorOf(rows[2]) == Graphite::DiffLineAnchor{Graphite::DiffSide::New, 4});
	CHECK(Graphite::AnchorOf(rows[3]) == Graphite::DiffLineAnchor{Graphite::DiffSide::New, 6});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);
	REQUIRE(pairs.size() == 3);

	CHECK_FALSE(Graphite::AnchorOf(pairs[0]).has_value());
	CHECK(Graphite::AnchorOf(pairs[1]) == Graphite::DiffLineAnchor{Graphite::DiffSide::New, 4});
}

TEST_CASE("A line of a hunk is found again in every layout of the same file")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	const Graphite::WholeFileRows whole = Graphite::BuildWholeFileRows(file, 6, 7);
	const std::vector<DiffRow> unified = Graphite::BuildUnifiedRows(file);
	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	const std::optional<Graphite::DiffLineAnchor> added = Graphite::AnchorOf(whole.Unified[4]);
	REQUIRE(added.has_value());

	CHECK(Graphite::RowAt(unified, *added) == Graphite::DiffRowAt{3, true});
	CHECK(Graphite::RowAt(pairs, *added) == Graphite::DiffRowAt{2, true});
	CHECK(Graphite::RowAt(whole.SideBySide, *added) == Graphite::DiffRowAt{3, true});

	const Graphite::DiffLineAnchor removed{Graphite::DiffSide::Old, 3};

	CHECK(Graphite::RowAt(unified, removed) == Graphite::DiffRowAt{1, true});
	CHECK(Graphite::RowAt(whole.SideBySide, removed) == Graphite::DiffRowAt{2, true});
}

TEST_CASE("A line outside every hunk lands on the header of the next hunk, not inside it")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	const std::vector<DiffRow> unified = Graphite::BuildUnifiedRows(file);
	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);
	const Graphite::DiffLineAnchor first{Graphite::DiffSide::New, 1};

	CHECK(Graphite::RowAt(unified, first) == Graphite::DiffRowAt{0, false});
	CHECK(Graphite::RowAt(pairs, first) == Graphite::DiffRowAt{0, false});
}

TEST_CASE("A line past the last hunk lands on the last row, and no rows land on none")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	const std::vector<DiffRow> unified = Graphite::BuildUnifiedRows(file);
	const Graphite::DiffLineAnchor tail{Graphite::DiffSide::New, 7};

	CHECK(Graphite::RowAt(unified, tail) == Graphite::DiffRowAt{3, false});
	CHECK(Graphite::RowAt(std::span<const DiffRow>{}, tail) == Graphite::DiffRowAt{0, false});
}

TEST_CASE("A switch keeps the row nearest the middle of the view, preferring one of a hunk")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	const Graphite::WholeFileRows whole = Graphite::BuildWholeFileRows(file, 6, 7);

	CHECK(Graphite::RowToKeep(whole.Unified, 0, 8) == std::optional<std::size_t>{3});

	CHECK(Graphite::RowToKeep(whole.Unified, 4, 8) == std::optional<std::size_t>{4});

	CHECK(Graphite::RowToKeep(whole.Unified, 5, 8) == std::optional<std::size_t>{6});

	CHECK(Graphite::RowToKeep(whole.Unified, 7, 50) == std::optional<std::size_t>{7});
	CHECK_FALSE(Graphite::RowToKeep(whole.Unified, 8, 12).has_value());

	const std::vector<DiffRow> unified = Graphite::BuildUnifiedRows(file);
	CHECK_FALSE(Graphite::RowToKeep(unified, 0, 1).has_value());

	CHECK(Graphite::RowToKeep(whole.SideBySide, 0, 7) == std::optional<std::size_t>{3});
}

namespace {

	Core::FileDiff TwoHunks()
	{
		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.Hunks.push_back(ExpandingHunk(3, 3));
		file.Hunks.push_back(ExpandingHunk(20, 21));
		return file;
	}

	bool IsMarker(const std::optional<DiffRow>& row)
	{
		return row.has_value() && (row->Kind == DiffRowKind::HunkHeader || row->Kind == DiffRowKind::HunkSignature || row->Kind == DiffRowKind::HunkGap);
	}
}

TEST_CASE("Hunks-only rows put a gap before every hunk but the first")
{
	const Core::FileDiff file = TwoHunks();

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 9);
	CHECK(rows[0].Kind == DiffRowKind::HunkHeader);
	CHECK(rows[4].Kind == DiffRowKind::HunkGap);
	CHECK(rows[4].Hunk == 1);
	CHECK(rows[5].Kind == DiffRowKind::HunkHeader);

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 7);
	REQUIRE(pairs[3].Left.has_value());
	REQUIRE(pairs[3].Right.has_value());
	CHECK(pairs[3].Left->Kind == DiffRowKind::HunkGap);
	CHECK(pairs[3].Right->Kind == DiffRowKind::HunkGap);
	CHECK(pairs[4].Left->Kind == DiffRowKind::HunkHeader);
}

TEST_CASE("Whole-file rows have no headers or gaps, since nothing is left out")
{
	const Graphite::WholeFileRows whole = Graphite::BuildWholeFileRows(TwoHunks(), 24, 26);

	for (const DiffRow& row : whole.Unified)
	{
		CHECK_FALSE(IsMarker(row));
	}

	for (const DiffPair& pair : whole.SideBySide)
	{
		CHECK_FALSE(IsMarker(pair.Left));
		CHECK_FALSE(IsMarker(pair.Right));
	}
}

TEST_CASE("A gap names no line and is never the row a switch keeps")
{
	const Core::FileDiff file = TwoHunks();
	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	CHECK(Graphite::DiffLineOf(file, rows[4]) == nullptr);
	CHECK_FALSE(Graphite::AnchorOf(rows[4]).has_value());
	CHECK_FALSE(Graphite::RowToKeep(rows, 4, 6).has_value());
}

TEST_CASE("A line between two hunks lands on the gap before the second")
{
	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(TwoHunks());

	CHECK(Graphite::RowAt(rows, Graphite::DiffLineAnchor{Graphite::DiffSide::New, 10}) == Graphite::DiffRowAt{4, false});
}

TEST_CASE("A hunk's scope is what follows its second @@, without the line break")
{
	using Graphite::HunkScope;

	CHECK(HunkScope("@@ -297,11 +319,18 @@ namespace Graphite {\n") == "namespace Graphite {");
	CHECK(HunkScope("@@ -1,2 +1,3 @@ int main()\r\n") == "int main()");
	CHECK(HunkScope("@@ -1,2 +1,3 @@\n").empty());
	CHECK(HunkScope("@@ -1,2 +1,3 @@").empty());

	CHECK(HunkScope("").empty());
	CHECK(HunkScope("namespace Graphite {").empty());
	CHECK(HunkScope("@@ -1,2 +1,3 namespace").empty());
}

TEST_CASE("A hunk's range reads as the lines it covers on that side")
{
	using Graphite::LineRangeLabel;

	CHECK(LineRangeLabel(297, 11) == "297–307");
	CHECK(LineRangeLabel(5, 1) == "5");

	CHECK(LineRangeLabel(4, 0) == "—");
}

TEST_CASE("A hunk whose header names a scope gets a row for it under the header")
{
	Core::FileDiff file = MakeFileDiff({MakeDiffLine(Core::DiffLineKind::Context, 1, 1, "one\n")});
	file.Hunks[0].Header = "@@ -1 +1 @@ namespace Graphite {\n";
	file.Hunks[0].OldCount = 1;
	file.Hunks[0].NewCount = 1;

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 3);
	CHECK(rows[0].Kind == DiffRowKind::HunkHeader);
	CHECK(rows[1].Kind == DiffRowKind::HunkSignature);
	CHECK(rows[1].Hunk == 0);
	CHECK(rows[2].Kind == DiffRowKind::Context);

	CHECK(Graphite::DiffLineOf(file, rows[1]) == nullptr);
	CHECK_FALSE(Graphite::AnchorOf(rows[1]).has_value());

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 3);
	REQUIRE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Left->Kind == DiffRowKind::HunkSignature);
	CHECK(pairs[1].Right->Kind == DiffRowKind::HunkSignature);

	const Graphite::WholeFileRows whole = Graphite::BuildWholeFileRows(file, 1, 1);

	REQUIRE(whole.Unified.size() == 1);
	CHECK(whole.Unified[0].Kind == DiffRowKind::Context);
}

TEST_CASE("A signature is found as the nearest line above the hunk that starts with it, indentation aside")
{
	using Graphite::SignatureLine;

	const std::vector<std::string_view> lines{"namespace Graphite {", "", "    int Foo()",
											  "    {", "        return 1;",
											  "    }"};

	CHECK(SignatureLine(lines, 5, "int Foo()") == 3);
	CHECK(SignatureLine(lines, 5, "namespace Graphite {") == 1);
	CHECK(SignatureLine(lines, 5, "int Bar()") == 0);

	CHECK(SignatureLine(lines, 5, "namespace Gra") == 1);

	CHECK(SignatureLine(lines, 3, "int Foo()") == 0);
	CHECK(SignatureLine(lines, 99, "int Foo()") == 3);
	CHECK(SignatureLine(lines, 5, "") == 0);
	CHECK(SignatureLine(std::span<const std::string_view>{}, 5, "namespace") == 0);
}
