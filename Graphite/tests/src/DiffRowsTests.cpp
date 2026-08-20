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
