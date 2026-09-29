#include <doctest/doctest.h>

#include "Git/DiffRows.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

using Graphite::DiffPair;
using Graphite::DiffRow;
using Graphite::DiffRowKind;

namespace {

	Core::DiffLine Line(Core::DiffLineKind kind, int oldLine, int newLine, std::string text)
	{
		Core::DiffLine line;
		line.Kind = kind;
		line.OldLine = oldLine;
		line.NewLine = newLine;
		line.Text = std::move(text);
		return line;
	}

	Core::FileDiff FileWith(std::vector<Core::DiffLine> lines)
	{
		Core::Hunk hunk;
		hunk.OldStart = 1;
		hunk.NewStart = 1;
		hunk.Header = "@@ -1 +1 @@\n";
		hunk.Lines = std::move(lines);

		Core::FileDiff file;
		file.Change = Core::FileChange::Modified;
		file.NewPath = "a.txt";
		file.Hunks.push_back(std::move(hunk));
		return file;
	}

	// A hunk that swaps one old line for two new ones, starting at the given position on
	// each side. Used to build whole-file fixtures where a hunk shifts the two sides apart.
	Core::Hunk ExpandingHunk(int oldStart, int newStart)
	{
		Core::Hunk hunk;
		hunk.OldStart = oldStart;
		hunk.OldCount = 1;
		hunk.NewStart = newStart;
		hunk.NewCount = 2;
		hunk.Header = "@@ hunk @@\n";
		hunk.Lines.push_back(Line(Core::DiffLineKind::Removed, oldStart, 0, "old\n"));
		hunk.Lines.push_back(Line(Core::DiffLineKind::Added, 0, newStart, "new-1\n"));
		hunk.Lines.push_back(Line(Core::DiffLineKind::Added, 0, newStart + 1, "new-2\n"));
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
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 2);
	CHECK(rows[0].Kind == DiffRowKind::HunkHeader);
	CHECK(rows[1].Kind == DiffRowKind::Context);
}

TEST_CASE("The unified view keeps the diff's own order")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n"),
										  Line(Core::DiffLineKind::Removed, 2, 0, "two\n"),
										  Line(Core::DiffLineKind::Added, 0, 2, "TWO\n"),
										  Line(Core::DiffLineKind::Context, 3, 3, "three\n")});

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 5);
	CHECK(rows[1].Kind == DiffRowKind::Context);
	CHECK(rows[2].Kind == DiffRowKind::Removed);
	CHECK(rows[3].Kind == DiffRowKind::Added);
	CHECK(rows[4].Kind == DiffRowKind::Context);
}

TEST_CASE("A row can find its own line back in the diff")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Added, 0, 1, "new\n")});

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 2);

	const Core::DiffLine* line = Graphite::LineOf(file, rows[1]);

	// The rows carry indices rather than pointers or copies, so the widget reads the text
	// straight out of the snapshot it is already holding without copying every line.
	REQUIRE(line != nullptr);
	CHECK(line->Text == "new\n");

	// A header row names no line of the file.
	CHECK(Graphite::LineOf(file, rows[0]) == nullptr);
}

TEST_CASE("LineOf returns null when a row's hunk index no longer fits the file")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});

	DiffRow row;
	row.Kind = DiffRowKind::Context;
	row.Hunk = 1; // The file has only one hunk, at index 0.
	row.Line = 0;

	CHECK(Graphite::LineOf(file, row) == nullptr);
}

TEST_CASE("LineOf returns null when a row's line index no longer fits its hunk")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});

	DiffRow row;
	row.Kind = DiffRowKind::Context;
	row.Hunk = 0;
	row.Line = 1; // The hunk has only one line, at index 0.

	CHECK(Graphite::LineOf(file, row) == nullptr);
}

TEST_CASE("Side by side puts a context line on both halves")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 2);
	REQUIRE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Left->OldLine == 1);
	CHECK(pairs[1].Right->NewLine == 1);
}

TEST_CASE("Side by side pairs a removed line with the added line that replaced it")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Removed, 2, 0, "two\n"),
										  Line(Core::DiffLineKind::Added, 0, 2, "TWO\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	// One content row, not two (plus the header row makes pairs.size() == 2): an edited
	// line is one change, and showing it as a deletion above an insertion is what the
	// side-by-side view exists to stop doing.
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
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Removed, 2, 0, "two\n"),
										  Line(Core::DiffLineKind::Removed, 3, 0, "three\n"),
										  Line(Core::DiffLineKind::Added, 0, 2, "TWO\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 3);

	CHECK(pairs[1].Left.has_value());
	CHECK(pairs[1].Right.has_value());

	// Two lines went, one came back. The second removal has nothing left in the added run
	// to pair with, so its right half must stay empty rather than pairing with anything.
	REQUIRE(pairs[2].Left.has_value());
	CHECK(pairs[2].Left->OldLine == 3);
	CHECK_FALSE(pairs[2].Right.has_value());
}

TEST_CASE("Side by side leaves the left half empty for a pure insertion")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Added, 0, 1, "new\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 2);
	CHECK_FALSE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Right->NewLine == 1);
}

TEST_CASE("A hunk header spans both halves")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE_FALSE(pairs.empty());
	REQUIRE(pairs[0].Left.has_value());
	CHECK(pairs[0].Left->Kind == DiffRowKind::HunkHeader);

	// Both halves carry it, so the widget draws one banner across the full width without
	// having to special-case the row.
	REQUIRE(pairs[0].Right.has_value());
	CHECK(pairs[0].Right->Kind == DiffRowKind::HunkHeader);
}

TEST_CASE("Rows from a second hunk name their own hunk")
{
	Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});

	Core::Hunk second;
	second.OldStart = 20;
	second.NewStart = 20;
	second.Header = "@@ -20 +20 @@\n";
	second.Lines.push_back(Line(Core::DiffLineKind::Added, 0, 20, "twenty\n"));
	file.Hunks.push_back(std::move(second));

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	// The second hunk opens with the gap standing for the lines between the two.
	REQUIRE(rows.size() == 5);
	CHECK(rows[0].Hunk == 0);
	CHECK(rows[1].Hunk == 0);
	CHECK(rows[2].Hunk == 1);
	CHECK(rows[3].Hunk == 1);
	CHECK(rows[4].Hunk == 1);

	// Staging works by hunk, so a row that cannot name its hunk cannot be acted on.
	const Core::DiffLine* line = Graphite::LineOf(file, rows[4]);
	REQUIRE(line != nullptr);
	CHECK(line->Text == "twenty\n");
}

TEST_CASE("Side by side rows from a second hunk name their own hunk")
{
	Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});

	Core::Hunk second;
	second.OldStart = 20;
	second.NewStart = 20;
	second.Header = "@@ -20 +20 @@\n";
	second.Lines.push_back(Line(Core::DiffLineKind::Added, 0, 20, "twenty\n"));
	file.Hunks.push_back(std::move(second));

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	// Header + context row for the first hunk; gap, header + one insertion row for the second.
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
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Added, 0, 1, "new\n"),
										  Line(Core::DiffLineKind::Context, 1, 2, "one\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 3);
	CHECK_FALSE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Right->Kind == DiffRowKind::Added);
	CHECK(pairs[1].Right->NewLine == 1);
}

TEST_CASE("Side by side pairs alternating removed and added lines in order, not by kind")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Removed, 1, 0, "one\n"),
										  Line(Core::DiffLineKind::Added, 0, 1, "ONE\n"),
										  Line(Core::DiffLineKind::Removed, 2, 0, "two\n"),
										  Line(Core::DiffLineKind::Added, 0, 2, "TWO\n")});

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	// Each Removed/Added pair is its own run, so this must come back as two paired rows,
	// not as a four-long removed run zipped against a four-long added run.
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

	for (std::size_t i = 0; i < 3; ++i)
	{
		CAPTURE(i);
		CHECK(rows.Unified[i].Kind == DiffRowKind::Unchanged);
		CHECK(rows.Unified[i].OldLine == static_cast<int>(i) + 1);
		CHECK(rows.Unified[i].NewLine == static_cast<int>(i) + 1);

		REQUIRE(rows.SideBySide[i].Left.has_value());
		REQUIRE(rows.SideBySide[i].Right.has_value());
		CHECK(rows.SideBySide[i].Left->OldLine == static_cast<int>(i) + 1);
		CHECK(rows.SideBySide[i].Right->NewLine == static_cast<int>(i) + 1);
	}
}

TEST_CASE("A hunk in the middle leaves Unchanged rows before and after it, resuming at the "
		  "right number on each side once the hunk has shifted them apart")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	// Old file: 6 lines, one of which (line 3) the hunk removes. New file: 7 lines, since
	// the hunk replaces that one line with two.
	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 6, 7);

	// Two Unchanged rows before the hunk, its three lines — no header, since nothing is left
	// out for one to introduce — and three Unchanged rows after: 2 + 3 + 3 = 8.
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

	// The old side resumes at 4, the new side at 5 — the hunk turned one old line into two
	// new ones, so the two sides have drifted a line apart from here on.
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

	// 2 zipped rows for the hunk (1 removal against 2 insertions), not 3: the side-by-side
	// view zips a hunk's runs exactly as BuildSideBySideRows does.
	REQUIRE(rows.SideBySide.size() == 7);

	// Before the hunk, nothing has shifted yet: both halves name the same line number.
	REQUIRE(rows.SideBySide[0].Left.has_value());
	REQUIRE(rows.SideBySide[0].Right.has_value());
	CHECK(rows.SideBySide[0].Left->OldLine == 1);
	CHECK(rows.SideBySide[0].Right->NewLine == 1);

	// After the hunk, the insertion has pushed the new side one line further along.
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

	// Three hunk lines, then Unchanged rows to the end of each side.
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

	// Old file ends at line 3, new file ends at line 4: the hunk covers every line from
	// where it starts to the end of both sides.
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

	// Each hunk turns one old line into two new ones, so the new side ends up two lines
	// ahead of the old side by the end of the file.
	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 12, 14);

	// Leading gap: old/new line 1 (1 line). Hunk 1: 3 lines. Gap between the hunks: old
	// lines 3..9 (7 lines), new lines 4..10 (7 lines). Hunk 2: 3 lines. Tail: old lines
	// 11..12, new lines 13..14 (2 lines).
	REQUIRE(rows.Unified.size() == 1 + 3 + 7 + 3 + 2);

	std::size_t index = 1 + 3; // Past the one-line leading gap and the first hunk.

	for (int oldLine = 3; oldLine <= 9; ++oldLine, ++index)
	{
		CAPTURE(oldLine);
		CHECK(rows.Unified[index].Kind == DiffRowKind::Unchanged);
		CHECK(rows.Unified[index].OldLine == oldLine);
		CHECK(rows.Unified[index].NewLine == oldLine + 1);
	}

	CHECK(rows.Unified[index].Kind == DiffRowKind::Removed);
	CHECK(rows.Unified[index].Hunk == 1);
}

TEST_CASE("An added file has no old side in its whole-file rows")
{
	Core::Hunk hunk;
	hunk.OldStart = 0;
	hunk.OldCount = 0;
	hunk.NewStart = 1;
	hunk.NewCount = 2;
	hunk.Header = "@@ -0,0 +1,2 @@\n";
	hunk.Lines.push_back(Line(Core::DiffLineKind::Added, 0, 1, "one\n"));
	hunk.Lines.push_back(Line(Core::DiffLineKind::Added, 0, 2, "two\n"));

	Core::FileDiff file;
	file.Change = Core::FileChange::Added;
	file.NewPath = "new.txt";
	file.Hunks.push_back(std::move(hunk));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 0, 2);

	// No gap on either side: the whole file is the one hunk's added lines.
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
	hunk.Lines.push_back(Line(Core::DiffLineKind::Removed, 1, 0, "one\n"));
	hunk.Lines.push_back(Line(Core::DiffLineKind::Removed, 2, 0, "two\n"));

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

TEST_CASE("LineOf returns null for an Unchanged row, which names no hunk to look a line up in")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});

	// Hunk and Line are left at their default zero, which is a real index into this file's
	// first hunk — LineOf must recognise the kind rather than trust the indices.
	DiffRow row;
	row.Kind = DiffRowKind::Unchanged;
	row.OldLine = 5;
	row.NewLine = 5;

	CHECK(Graphite::LineOf(file, row) == nullptr);
}

// A pure insertion, which git writes as "@@ -3,0 +4,2 @@": the old side contributes no line
// of its own, so OldStart names the last old line before the insertion rather than a line in
// the hunk. Sitting inside an otherwise two-sided file (both oldLineCount and newLineCount
// are positive), this is what tells apart choosing the gap length from this hunk's own
// NewCount (correct, since it is positive) from trusting OldStart because the file has an
// old side at all (wrong, since this hunk's OldCount is zero) — every earlier fixture had
// both counts positive, where either choice gives the same answer.
TEST_CASE("A pure insertion inside a two-sided file resumes both sides at the right numbers")
{
	Core::Hunk hunk;
	hunk.OldStart = 3;
	hunk.OldCount = 0;
	hunk.NewStart = 4;
	hunk.NewCount = 2;
	hunk.Header = "@@ -3,0 +4,2 @@\n";
	hunk.Lines.push_back(Line(Core::DiffLineKind::Added, 0, 4, "ins-1\n"));
	hunk.Lines.push_back(Line(Core::DiffLineKind::Added, 0, 5, "ins-2\n"));

	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.NewPath = "a.txt";
	file.Hunks.push_back(std::move(hunk));

	// Old file: 6 lines. New file: 8, since 2 were inserted after old line 3 / new line 3.
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

	// The old side did not move at all — it resumes at old line 4, right where it left off —
	// while the new side has jumped ahead by the two inserted lines.
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

// The symmetric case: a pure deletion, which git writes as "@@ -4,2 +3,0 @@". Here NewStart
// names the last new line before the deletion rather than a line the hunk covers, so the gap
// length must come from OldCount (positive) rather than from the file having a new side at
// all (this hunk's NewCount is zero).
TEST_CASE("A pure deletion inside a two-sided file resumes both sides at the right numbers")
{
	Core::Hunk hunk;
	hunk.OldStart = 4;
	hunk.OldCount = 2;
	hunk.NewStart = 3;
	hunk.NewCount = 0;
	hunk.Header = "@@ -4,2 +3,0 @@\n";
	hunk.Lines.push_back(Line(Core::DiffLineKind::Removed, 4, 0, "del-1\n"));
	hunk.Lines.push_back(Line(Core::DiffLineKind::Removed, 5, 0, "del-2\n"));

	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.NewPath = "a.txt";
	file.Hunks.push_back(std::move(hunk));

	// Old file: 8 lines. New file: 6, since old lines 4-5 were deleted with nothing added.
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

	// The new side did not move — it resumes at new line 4 — while the old side has jumped
	// ahead by the two deleted lines.
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

// DiffRows.h warns that a caller can hand in line counts from a different snapshot than the
// diff. Here the hunk covers old/new lines 1-3, but the counts passed in claim the file has
// only 1 line on each side — the exact skew that warning describes. Without ClampedGap, the
// tail computation `newLineCount + 1 - newNext` becomes `2 - 4` as an unsigned subtraction,
// wrapping to roughly 1.8e19 and driving emitGap's loop until the process runs out of memory
// (or hangs first) rather than failing fast. This test cannot be run against that unclamped
// arithmetic to watch it fail — doing so would hang or exhaust memory rather than report a
// wrong number, which is the whole point of the clamp — so it is verified only against the
// delivered, clamped code, asserting an exact small row count rather than merely "not huge".
TEST_CASE("A line count smaller than the hunk's own numbers does not blow up the tail gap")
{
	Core::Hunk hunk;
	hunk.OldStart = 1;
	hunk.OldCount = 3;
	hunk.NewStart = 1;
	hunk.NewCount = 3;
	hunk.Header = "@@ -1,3 +1,3 @@\n";
	hunk.Lines.push_back(Line(Core::DiffLineKind::Context, 1, 1, "one\n"));
	hunk.Lines.push_back(Line(Core::DiffLineKind::Context, 2, 2, "two\n"));
	hunk.Lines.push_back(Line(Core::DiffLineKind::Context, 3, 3, "three\n"));

	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.NewPath = "a.txt";
	file.Hunks.push_back(std::move(hunk));

	// The hunk's own OldStart+OldCount and NewStart+NewCount both reach line 4; these counts
	// say the file has only 1 line per side, so the tail gap the hunk leaves open behind it
	// would go negative if computed without a clamp.
	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 1, 1);

	// No leading gap (the hunk starts at line 1) and no tail (the clamp floors it at zero):
	// just the hunk's own 3 lines, exactly.
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

	// One token covering a whole line, so a test can tell which side's tokens came back by
	// the kind alone.
	std::vector<Graphite::Token> OneToken(Graphite::TokenKind kind, std::size_t length)
	{
		return {Graphite::Token{kind, 0, length}};
	}
}

TEST_CASE("An Unchanged row past the end of both sides resolves to nothing")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});
	const std::vector<std::string_view> lines{"one"};

	// The rows outlived the texts they were built against - the only way this happens - and
	// the answer is to draw nothing rather than to read past either vector.
	const Graphite::ResolvedLine resolved = Graphite::ResolveRow(
		file, UnchangedRow(9, 9), Graphite::DiffSide::New, lines, lines, Graphite::FileTokens{});

	CHECK_FALSE(resolved.Text.has_value());
	CHECK(resolved.Tokens == nullptr);
}

TEST_CASE("An Unchanged row resolves against the side it is drawn for")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "same\n")});
	const std::vector<std::string_view> oldLines{"old"};
	const std::vector<std::string_view> newLines{"new"};

	Graphite::FileTokens tokens;
	tokens.Old.push_back(OneToken(Graphite::TokenKind::Comment, 3));
	tokens.New.push_back(OneToken(Graphite::TokenKind::Keyword, 3));

	const Graphite::ResolvedLine left = Graphite::ResolveRow(
		file, UnchangedRow(1, 1), Graphite::DiffSide::Old, oldLines, newLines, tokens);
	const Graphite::ResolvedLine right = Graphite::ResolveRow(
		file, UnchangedRow(1, 1), Graphite::DiffSide::New, oldLines, newLines, tokens);

	// The two halves say the same thing by construction, but they are not lexed the same
	// way: a rename from foo.txt to foo.cpp has a different language on each side, and the
	// old half must not be coloured by the new file's lexer.
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
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Removed, 1, 0, "gone\n")});
	const std::vector<std::string_view> oldLines{"only-old"};

	// A deleted file has no new side at all, so its rows carry no new line number. The half
	// being drawn still has to show something.
	const Graphite::ResolvedLine resolved =
		Graphite::ResolveRow(file, UnchangedRow(1, 0), Graphite::DiffSide::New, oldLines,
							 std::vector<std::string_view>{}, Graphite::FileTokens{});

	REQUIRE(resolved.Text.has_value());
	CHECK(*resolved.Text == "only-old");
}

TEST_CASE("A token vector shorter than the file leaves the later lines uncoloured")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});
	const std::vector<std::string_view> lines{"one", "two"};

	Graphite::FileTokens tokens;
	tokens.New.push_back(OneToken(Graphite::TokenKind::Keyword, 3));

	const Graphite::ResolvedLine first = Graphite::ResolveRow(
		file, UnchangedRow(1, 1), Graphite::DiffSide::New, lines, lines, tokens);
	const Graphite::ResolvedLine second = Graphite::ResolveRow(
		file, UnchangedRow(2, 2), Graphite::DiffSide::New, lines, lines, tokens);

	// The lines and the tokens are bounded separately: a lexer that produced fewer lines than
	// the split did must leave the tail plain, not read past its own end.
	CHECK(first.Tokens != nullptr);
	REQUIRE(second.Text.has_value());
	CHECK(*second.Text == "two");
	CHECK(second.Tokens == nullptr);
}

TEST_CASE("A changed row draws its own text without the line terminator")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Added, 0, 1, "added\r\n")});

	DiffRow row;
	row.Kind = DiffRowKind::Added;
	row.NewLine = 1;
	row.Hunk = 0;
	row.Line = 0;

	const Graphite::ResolvedLine resolved =
		Graphite::ResolveRow(file, row, Graphite::DiffSide::New, std::vector<std::string_view>{},
							 std::vector<std::string_view>{}, Graphite::FileTokens{});

	// Stripped by the same rule SplitTextLines uses, so a whole-file row and a hunk row of
	// the same line agree on exactly which bytes a token's From/To index.
	REQUIRE(resolved.Text.has_value());
	CHECK(*resolved.Text == "added");
}

TEST_CASE("A changed row whose indices no longer fit resolves to nothing")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Added, 0, 1, "added\n")});

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
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Removed, 4, 0, "gone\n"),
										  Line(Core::DiffLineKind::Added, 0, 4, "come\n"),
										  Line(Core::DiffLineKind::Context, 5, 6, "kept\n")});

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);
	REQUIRE(rows.size() == 4);

	CHECK_FALSE(Graphite::AnchorOf(rows[0]).has_value());
	CHECK(Graphite::AnchorOf(rows[1]) == Graphite::DiffLineAnchor{Graphite::DiffSide::Old, 4});
	CHECK(Graphite::AnchorOf(rows[2]) == Graphite::DiffLineAnchor{Graphite::DiffSide::New, 4});
	CHECK(Graphite::AnchorOf(rows[3]) == Graphite::DiffLineAnchor{Graphite::DiffSide::New, 6});

	// The removed and added lines share a row side by side, which stands for the added one.
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

	// The second added line: row 4 of the whole file, after two Unchanged rows, the removed
	// line and the first added one.
	const std::optional<Graphite::DiffLineAnchor> added = Graphite::AnchorOf(whole.Unified[4]);
	REQUIRE(added.has_value());

	CHECK(Graphite::RowAt(unified, *added) == Graphite::DiffRowAt{3, true});
	CHECK(Graphite::RowAt(pairs, *added) == Graphite::DiffRowAt{2, true});
	CHECK(Graphite::RowAt(whole.SideBySide, *added) == Graphite::DiffRowAt{3, true});

	// The removed line is found by its old number, in the row that pairs it off.
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

	// The first row with a new-side line is the first added one, behind the header and a
	// removed line that has none.
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

	// Two Unchanged rows, the removed line, two added, three Unchanged.
	const Graphite::WholeFileRows whole = Graphite::BuildWholeFileRows(file, 6, 7);

	// The middle of rows 0 to 7 falls between 3 and 4, both in the hunk; the first wins.
	CHECK(Graphite::RowToKeep(whole.Unified, 0, 8) == std::optional<std::size_t>{3});

	// The middle of rows 4 to 7 falls between 5 and 6, both Unchanged; the added line at 4
	// is further from it but is kept, being the one the hunks-only rows also have.
	CHECK(Graphite::RowToKeep(whole.Unified, 4, 8) == std::optional<std::size_t>{4});

	// Nothing of the hunk on screen: the Unchanged row nearest the middle.
	CHECK(Graphite::RowToKeep(whole.Unified, 5, 8) == std::optional<std::size_t>{6});

	// A span past the rows is cut to them.
	CHECK(Graphite::RowToKeep(whole.Unified, 7, 50) == std::optional<std::size_t>{7});
	CHECK_FALSE(Graphite::RowToKeep(whole.Unified, 8, 12).has_value());

	// A view down to a header alone keeps nothing.
	const std::vector<DiffRow> unified = Graphite::BuildUnifiedRows(file);
	CHECK_FALSE(Graphite::RowToKeep(unified, 0, 1).has_value());

	// Side by side the same: the second added line, alone in its pair, sits in the middle.
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
}

TEST_CASE("Hunks-only rows put a gap before every hunk but the first")
{
	const Core::FileDiff file = TwoHunks();

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	// Header and three lines per hunk, and one gap between them.
	REQUIRE(rows.size() == 9);
	CHECK(rows[0].Kind == DiffRowKind::HunkHeader);
	CHECK(rows[4].Kind == DiffRowKind::HunkGap);
	CHECK(rows[4].Hunk == 1);
	CHECK(rows[5].Kind == DiffRowKind::HunkHeader);

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	// Header, removed-against-added, the second added line: three pairs per hunk.
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

	const auto marks = [](const std::optional<DiffRow>& row) {
		return row.has_value()
			   && (row->Kind == DiffRowKind::HunkHeader || row->Kind == DiffRowKind::HunkSignature
				   || row->Kind == DiffRowKind::HunkGap);
	};

	for (const DiffRow& row : whole.Unified)
	{
		CHECK_FALSE(marks(row));
	}

	for (const DiffPair& pair : whole.SideBySide)
	{
		CHECK_FALSE(marks(pair.Left));
		CHECK_FALSE(marks(pair.Right));
	}
}

TEST_CASE("A gap names no line and is never the row a switch keeps")
{
	const Core::FileDiff file = TwoHunks();
	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	CHECK(Graphite::LineOf(file, rows[4]) == nullptr);
	CHECK_FALSE(Graphite::AnchorOf(rows[4]).has_value());
	CHECK_FALSE(Graphite::RowToKeep(rows, 4, 6).has_value());
}

TEST_CASE("A line between two hunks lands on the gap before the second")
{
	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(TwoHunks());

	// Line 10 of the new file is between the hunks, which the hunks-only rows leave out.
	CHECK(Graphite::RowAt(rows, Graphite::DiffLineAnchor{Graphite::DiffSide::New, 10})
		  == Graphite::DiffRowAt{4, false});
}

TEST_CASE("A hunk's scope is what follows its second @@, without the line break")
{
	using Graphite::HunkScope;

	CHECK(HunkScope("@@ -297,11 +319,18 @@ namespace Graphite {\n") == "namespace Graphite {");
	CHECK(HunkScope("@@ -1,2 +1,3 @@ int main()\r\n") == "int main()");
	CHECK(HunkScope("@@ -1,2 +1,3 @@\n").empty());
	CHECK(HunkScope("@@ -1,2 +1,3 @@").empty());

	// Not a header at all, or one missing its closing marker: nothing to show.
	CHECK(HunkScope("").empty());
	CHECK(HunkScope("namespace Graphite {").empty());
	CHECK(HunkScope("@@ -1,2 +1,3 namespace").empty());
}

TEST_CASE("A hunk's range reads as the lines it covers on that side")
{
	using Graphite::LineRangeLabel;

	CHECK(LineRangeLabel(297, 11) == "297–307");
	CHECK(LineRangeLabel(5, 1) == "5");

	// A side the hunk has no lines on — a pure insertion's old side — covers nothing.
	CHECK(LineRangeLabel(4, 0) == "—");
}

TEST_CASE("A hunk whose header names a scope gets a row for it under the header")
{
	Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "one\n")});
	file.Hunks[0].Header = "@@ -1 +1 @@ namespace Graphite {\n";
	file.Hunks[0].OldCount = 1;
	file.Hunks[0].NewCount = 1;

	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	REQUIRE(rows.size() == 3);
	CHECK(rows[0].Kind == DiffRowKind::HunkHeader);
	CHECK(rows[1].Kind == DiffRowKind::HunkSignature);
	CHECK(rows[1].Hunk == 0);
	CHECK(rows[2].Kind == DiffRowKind::Context);

	// It names no line, so nothing can resolve it or keep it.
	CHECK(Graphite::LineOf(file, rows[1]) == nullptr);
	CHECK_FALSE(Graphite::AnchorOf(rows[1]).has_value());

	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);

	REQUIRE(pairs.size() == 3);
	REQUIRE(pairs[1].Left.has_value());
	REQUIRE(pairs[1].Right.has_value());
	CHECK(pairs[1].Left->Kind == DiffRowKind::HunkSignature);
	CHECK(pairs[1].Right->Kind == DiffRowKind::HunkSignature);

	// The whole file has neither: nothing is left out for them to introduce.
	const Graphite::WholeFileRows whole = Graphite::BuildWholeFileRows(file, 1, 1);

	REQUIRE(whole.Unified.size() == 1);
	CHECK(whole.Unified[0].Kind == DiffRowKind::Context);
}

TEST_CASE("A signature is found as the nearest line above the hunk that starts with it")
{
	using Graphite::SignatureLine;

	const std::vector<std::string_view> lines{"namespace Graphite {", "",   "    int Foo()",
											  "    {",                 "        return 1;",
											  "    }"};

	// Searched upward from the line before the hunk, which starts at line 5 here. Indentation
	// is not compared: HunkScope trims it off the scope, having no way to tell it from the
	// space git puts after the second "@@".
	CHECK(SignatureLine(lines, 5, "int Foo()") == 3);
	CHECK(SignatureLine(lines, 5, "namespace Graphite {") == 1);
	CHECK(SignatureLine(lines, 5, "int Bar()") == 0);

	// Git cuts a long line short, so the scope may be only the line's beginning.
	CHECK(SignatureLine(lines, 5, "namespace Gra") == 1);

	// Never the hunk's own lines or past the file, and never an empty scope.
	CHECK(SignatureLine(lines, 3, "int Foo()") == 0);
	CHECK(SignatureLine(lines, 99, "int Foo()") == 3);
	CHECK(SignatureLine(lines, 5, "") == 0);
	CHECK(SignatureLine(std::span<const std::string_view>{}, 5, "namespace") == 0);
}
