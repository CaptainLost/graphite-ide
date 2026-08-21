#include <doctest/doctest.h>

#include "Git/DiffRows.h"

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

	REQUIRE(rows.size() == 4);
	CHECK(rows[0].Hunk == 0);
	CHECK(rows[1].Hunk == 0);
	CHECK(rows[2].Hunk == 1);
	CHECK(rows[3].Hunk == 1);

	// Staging works by hunk, so a row that cannot name its hunk cannot be acted on.
	const Core::DiffLine* line = Graphite::LineOf(file, rows[3]);
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

	// Header + context row for the first hunk, header + one insertion row for the second.
	REQUIRE(pairs.size() == 4);
	CHECK(pairs[0].Left->Hunk == 0);
	CHECK(pairs[1].Left->Hunk == 0);
	CHECK(pairs[2].Left->Hunk == 1);

	REQUIRE(pairs[3].Right.has_value());
	CHECK(pairs[3].Right->Hunk == 1);
	CHECK_FALSE(pairs[3].Left.has_value());
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

	// Two Unchanged rows before the hunk, a header plus three hunk lines, three Unchanged
	// rows after: 2 + 4 + 3 = 9.
	REQUIRE(rows.Unified.size() == 9);

	CHECK(rows.Unified[0].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[0].OldLine == 1);
	CHECK(rows.Unified[0].NewLine == 1);
	CHECK(rows.Unified[1].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[1].OldLine == 2);
	CHECK(rows.Unified[1].NewLine == 2);

	CHECK(rows.Unified[2].Kind == DiffRowKind::HunkHeader);
	CHECK(rows.Unified[3].Kind == DiffRowKind::Removed);
	CHECK(rows.Unified[4].Kind == DiffRowKind::Added);
	CHECK(rows.Unified[5].Kind == DiffRowKind::Added);

	// The old side resumes at 4, the new side at 5 — the hunk turned one old line into two
	// new ones, so the two sides have drifted a line apart from here on.
	CHECK(rows.Unified[6].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[6].OldLine == 4);
	CHECK(rows.Unified[6].NewLine == 5);
	CHECK(rows.Unified[7].OldLine == 5);
	CHECK(rows.Unified[7].NewLine == 6);
	CHECK(rows.Unified[8].OldLine == 6);
	CHECK(rows.Unified[8].NewLine == 7);
}

TEST_CASE("Side by side keeps an Unchanged row's two halves in step before a hunk and drifts "
		  "them apart after it")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 6, 7);

	// Header + 2 zipped rows for the hunk (1 removal against 2 insertions), not 4: the
	// side-by-side view zips a hunk's runs exactly as BuildSideBySideRows does.
	REQUIRE(rows.SideBySide.size() == 8);

	// Before the hunk, nothing has shifted yet: both halves name the same line number.
	REQUIRE(rows.SideBySide[0].Left.has_value());
	REQUIRE(rows.SideBySide[0].Right.has_value());
	CHECK(rows.SideBySide[0].Left->OldLine == 1);
	CHECK(rows.SideBySide[0].Right->NewLine == 1);

	// After the hunk, the insertion has pushed the new side one line further along.
	REQUIRE(rows.SideBySide[5].Left.has_value());
	REQUIRE(rows.SideBySide[5].Right.has_value());
	CHECK(rows.SideBySide[5].Left->OldLine == 4);
	CHECK(rows.SideBySide[5].Right->NewLine == 5);
}

TEST_CASE("A hunk at the very first line leaves no Unchanged row ahead of it")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(1, 1));

	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 4, 5);

	// A header plus three hunk lines, then Unchanged rows to the end of each side.
	REQUIRE(rows.Unified.size() == 4 + 3);
	CHECK(rows.Unified[0].Kind == DiffRowKind::HunkHeader);

	CHECK(rows.Unified[4].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[4].OldLine == 2);
	CHECK(rows.Unified[4].NewLine == 3);
}

TEST_CASE("A hunk at the very last line leaves no Unchanged row after it")
{
	Core::FileDiff file;
	file.Change = Core::FileChange::Modified;
	file.Hunks.push_back(ExpandingHunk(3, 3));

	// Old file ends at line 3, new file ends at line 4: the hunk covers every line from
	// where it starts to the end of both sides.
	const Graphite::WholeFileRows rows = Graphite::BuildWholeFileRows(file, 3, 4);

	REQUIRE(rows.Unified.size() == 2 + 4);
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

	// Leading gap: old/new line 1 (1 line). Hunk 1: header + 3 lines. Gap between the hunks:
	// old lines 3..9 (7 lines), new lines 4..10 (7 lines). Hunk 2: header + 3 lines. Tail:
	// old lines 11..12, new lines 13..14 (2 lines).
	REQUIRE(rows.Unified.size() == 1 + 4 + 7 + 4 + 2);

	std::size_t index = 1 + 4; // Past the one-line leading gap and the first hunk.

	for (int oldLine = 3; oldLine <= 9; ++oldLine, ++index)
	{
		CAPTURE(oldLine);
		CHECK(rows.Unified[index].Kind == DiffRowKind::Unchanged);
		CHECK(rows.Unified[index].OldLine == oldLine);
		CHECK(rows.Unified[index].NewLine == oldLine + 1);
	}

	CHECK(rows.Unified[index].Kind == DiffRowKind::HunkHeader);
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
	REQUIRE(rows.Unified.size() == 3);
	CHECK(rows.Unified[0].Kind == DiffRowKind::HunkHeader);
	CHECK(rows.Unified[1].OldLine == 0);
	CHECK(rows.Unified[2].OldLine == 0);

	REQUIRE(rows.SideBySide.size() == 3);
	CHECK_FALSE(rows.SideBySide[1].Left.has_value());
	CHECK_FALSE(rows.SideBySide[2].Left.has_value());
	REQUIRE(rows.SideBySide[1].Right.has_value());
	REQUIRE(rows.SideBySide[2].Right.has_value());
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

	REQUIRE(rows.Unified.size() == 3);
	CHECK(rows.Unified[0].Kind == DiffRowKind::HunkHeader);
	CHECK(rows.Unified[1].NewLine == 0);
	CHECK(rows.Unified[2].NewLine == 0);

	REQUIRE(rows.SideBySide.size() == 3);
	CHECK_FALSE(rows.SideBySide[1].Right.has_value());
	CHECK_FALSE(rows.SideBySide[2].Right.has_value());
	REQUIRE(rows.SideBySide[1].Left.has_value());
	REQUIRE(rows.SideBySide[2].Left.has_value());
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

	REQUIRE(rows.Unified.size() == 9);

	CHECK(rows.Unified[0].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[0].OldLine == 1);
	CHECK(rows.Unified[0].NewLine == 1);
	CHECK(rows.Unified[1].OldLine == 2);
	CHECK(rows.Unified[1].NewLine == 2);
	CHECK(rows.Unified[2].OldLine == 3);
	CHECK(rows.Unified[2].NewLine == 3);

	CHECK(rows.Unified[3].Kind == DiffRowKind::HunkHeader);
	CHECK(rows.Unified[4].Kind == DiffRowKind::Added);
	CHECK(rows.Unified[4].NewLine == 4);
	CHECK(rows.Unified[5].Kind == DiffRowKind::Added);
	CHECK(rows.Unified[5].NewLine == 5);

	// The old side did not move at all — it resumes at old line 4, right where it left off —
	// while the new side has jumped ahead by the two inserted lines.
	CHECK(rows.Unified[6].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[6].OldLine == 4);
	CHECK(rows.Unified[6].NewLine == 6);
	CHECK(rows.Unified[7].OldLine == 5);
	CHECK(rows.Unified[7].NewLine == 7);
	CHECK(rows.Unified[8].OldLine == 6);
	CHECK(rows.Unified[8].NewLine == 8);

	REQUIRE(rows.SideBySide.size() == 9);
	CHECK_FALSE(rows.SideBySide[4].Left.has_value());
	REQUIRE(rows.SideBySide[4].Right.has_value());
	CHECK(rows.SideBySide[4].Right->NewLine == 4);
	CHECK_FALSE(rows.SideBySide[5].Left.has_value());
	REQUIRE(rows.SideBySide[5].Right.has_value());
	CHECK(rows.SideBySide[5].Right->NewLine == 5);

	REQUIRE(rows.SideBySide[6].Left.has_value());
	REQUIRE(rows.SideBySide[6].Right.has_value());
	CHECK(rows.SideBySide[6].Left->OldLine == 4);
	CHECK(rows.SideBySide[6].Right->NewLine == 6);
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

	REQUIRE(rows.Unified.size() == 9);

	CHECK(rows.Unified[0].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[0].OldLine == 1);
	CHECK(rows.Unified[0].NewLine == 1);
	CHECK(rows.Unified[1].OldLine == 2);
	CHECK(rows.Unified[1].NewLine == 2);
	CHECK(rows.Unified[2].OldLine == 3);
	CHECK(rows.Unified[2].NewLine == 3);

	CHECK(rows.Unified[3].Kind == DiffRowKind::HunkHeader);
	CHECK(rows.Unified[4].Kind == DiffRowKind::Removed);
	CHECK(rows.Unified[4].OldLine == 4);
	CHECK(rows.Unified[5].Kind == DiffRowKind::Removed);
	CHECK(rows.Unified[5].OldLine == 5);

	// The new side did not move — it resumes at new line 4 — while the old side has jumped
	// ahead by the two deleted lines.
	CHECK(rows.Unified[6].Kind == DiffRowKind::Unchanged);
	CHECK(rows.Unified[6].OldLine == 6);
	CHECK(rows.Unified[6].NewLine == 4);
	CHECK(rows.Unified[7].OldLine == 7);
	CHECK(rows.Unified[7].NewLine == 5);
	CHECK(rows.Unified[8].OldLine == 8);
	CHECK(rows.Unified[8].NewLine == 6);

	REQUIRE(rows.SideBySide.size() == 9);
	REQUIRE(rows.SideBySide[4].Left.has_value());
	CHECK_FALSE(rows.SideBySide[4].Right.has_value());
	CHECK(rows.SideBySide[4].Left->OldLine == 4);
	REQUIRE(rows.SideBySide[5].Left.has_value());
	CHECK_FALSE(rows.SideBySide[5].Right.has_value());
	CHECK(rows.SideBySide[5].Left->OldLine == 5);

	REQUIRE(rows.SideBySide[6].Left.has_value());
	REQUIRE(rows.SideBySide[6].Right.has_value());
	CHECK(rows.SideBySide[6].Left->OldLine == 6);
	CHECK(rows.SideBySide[6].Right->NewLine == 4);
}
