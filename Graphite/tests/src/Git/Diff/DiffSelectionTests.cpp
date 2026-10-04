#include <doctest/doctest.h>

#include "Git/Diff/DiffSelection.h"

#include "Core/Text/TextSelection.h"

#include <string>
#include <string_view>
#include <vector>

using Graphite::DiffPair;
using Graphite::DiffRow;
using Graphite::DiffSide;

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

	// a, b -> B, c: context, removed, added, context.
	Core::FileDiff EditedFile()
	{
		return FileWith({Line(Core::DiffLineKind::Context, 1, 1, "a\n"),
						 Line(Core::DiffLineKind::Removed, 2, 0, "b\n"),
						 Line(Core::DiffLineKind::Added, 0, 2, "B\n"),
						 Line(Core::DiffLineKind::Context, 3, 3, "c\n")});
	}

	// One byte per codepoint is 10 wide, so a multi-byte character is as wide as an ASCII one.
	float MonoWidth(std::string_view prefix)
	{
		float width = 0.0f;

		for (const char c : prefix)
		{
			if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
			{
				width += 10.0f;
			}
		}

		return width;
	}
}

TEST_CASE("A unified selection joins the code it covers and skips headers")
{
	const Core::FileDiff file = EditedFile();
	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);
	REQUIRE(rows.size() == 5);

	Core::TextSelection selection;
	selection.Select({1, 0}, {4, 1});

	const std::string text = Graphite::SelectedUnifiedText(selection, rows, file, {}, {}, {});

	CHECK(text == "a\nb\nB\nc");
}

TEST_CASE("A unified selection that starts on the header leaves the header out")
{
	const Core::FileDiff file = EditedFile();
	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	Core::TextSelection selection;
	selection.Select({0, 0}, {1, 1});

	CHECK(Graphite::SelectedUnifiedText(selection, rows, file, {}, {}, {}) == "a");
	CHECK_FALSE(Graphite::UnifiedLineOf(rows, 0).has_value());
	CHECK(Graphite::UnifiedLineOf(rows, 3) == 3);
	CHECK_FALSE(Graphite::UnifiedLineOf(rows, 99).has_value());
}

TEST_CASE("A side-by-side selection numbers lines by the file line on its side")
{
	const Core::FileDiff file = EditedFile();
	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);
	REQUIRE(pairs.size() == 4);

	// header, a|a, b|B, c|c
	CHECK_FALSE(Graphite::SideLineOf(pairs[0], DiffSide::Old).has_value());
	CHECK(Graphite::SideLineOf(pairs[2], DiffSide::Old) == 2);
	CHECK(Graphite::SideLineOf(pairs[2], DiffSide::New) == 2);

	Core::TextSelection selection;
	selection.Select({1, 0}, {3, 1});

	CHECK(Graphite::SelectedSideText(selection, DiffSide::Old, pairs, file, {}, {}, {}) == "a\nb\nc");
	CHECK(Graphite::SelectedSideText(selection, DiffSide::New, pairs, file, {}, {}, {}) == "a\nB\nc");
}

TEST_CASE("A pair with no row on a side has no line there and adds nothing")
{
	const Core::FileDiff file = FileWith({Line(Core::DiffLineKind::Context, 1, 1, "a\n"),
										  Line(Core::DiffLineKind::Added, 0, 2, "new\n"),
										  Line(Core::DiffLineKind::Context, 2, 3, "c\n")});
	const std::vector<DiffPair> pairs = Graphite::BuildSideBySideRows(file);
	REQUIRE(pairs.size() == 4);
	REQUIRE_FALSE(pairs[2].Left.has_value());
	REQUIRE(pairs[2].Right.has_value());

	CHECK_FALSE(Graphite::SideLineOf(pairs[2], DiffSide::Old).has_value());
	CHECK(Graphite::SideLineOf(pairs[2], DiffSide::New) == 2);

	Core::TextSelection selection;
	selection.Select({1, 0}, {3, 1});

	CHECK(Graphite::SelectedSideText(selection, DiffSide::Old, pairs, file, {}, {}, {}) == "a\nc");
	CHECK(Graphite::SelectedSideText(selection, DiffSide::New, pairs, file, {}, {}, {}) == "a\nnew\nc");
}

TEST_CASE("An empty selection yields no text")
{
	const Core::FileDiff file = EditedFile();
	const std::vector<DiffRow> rows = Graphite::BuildUnifiedRows(file);

	CHECK(Graphite::SelectedUnifiedText(Core::TextSelection{}, rows, file, {}, {}, {}).empty());
}

TEST_CASE("ByteAtX picks the nearest character boundary")
{
	CHECK(Graphite::ByteAtX("ab", 14.0f, MonoWidth) == 1);
	CHECK(Graphite::ByteAtX("ab", 16.0f, MonoWidth) == 2);
	CHECK(Graphite::ByteAtX("ab", 4.0f, MonoWidth) == 0);
	CHECK(Graphite::ByteAtX("ab", -5.0f, MonoWidth) == 0);
	CHECK(Graphite::ByteAtX("ab", 500.0f, MonoWidth) == 2);
	CHECK(Graphite::ByteAtX("", 5.0f, MonoWidth) == 0);
}

TEST_CASE("ByteAtX never lands inside a multi-byte character")
{
	const std::string_view text = "\xC5\xBC\xC3\xB3"; // zo with dot above, o with acute

	CHECK(Graphite::ByteAtX(text, 11.0f, MonoWidth) == 2);
	CHECK(Graphite::ByteAtX(text, 2.0f, MonoWidth) == 0);
	CHECK(Graphite::ByteAtX(text, 19.0f, MonoWidth) == 4);
}

TEST_CASE("ByteUnderX picks the character whose extent holds x")
{
	CHECK(Graphite::ByteUnderX("ab", 0.0f, MonoWidth) == 0);
	CHECK(Graphite::ByteUnderX("ab", 9.0f, MonoWidth) == 0);
	CHECK(Graphite::ByteUnderX("ab", 16.0f, MonoWidth) == 1);
	CHECK(Graphite::ByteUnderX("ab", 20.0f, MonoWidth) == 2);
	CHECK(Graphite::ByteUnderX("ab", 500.0f, MonoWidth) == 2);
	CHECK(Graphite::ByteUnderX("ab", -3.0f, MonoWidth) == 0);
	CHECK(Graphite::ByteUnderX("", 5.0f, MonoWidth) == 0);
}

TEST_CASE("ByteUnderX returns the start of a multi-byte character")
{
	const std::string_view text = "\xC5\xBC\xC3\xB3";

	CHECK(Graphite::ByteUnderX(text, 5.0f, MonoWidth) == 0);
	CHECK(Graphite::ByteUnderX(text, 11.0f, MonoWidth) == 2);
	CHECK(Graphite::ByteUnderX(text, 19.0f, MonoWidth) == 2);
	CHECK(Graphite::ByteUnderX(text, 20.0f, MonoWidth) == 4);
}

TEST_CASE("A selection context differs when anything it depends on does")
{
	Graphite::DiffSelectionContext base;
	base.Path = "a.txt";
	base.Mode = Graphite::DiffViewMode::Unified;
	base.Scope = Graphite::DiffScope::Hunks;
	base.Side = DiffSide::New;
	base.ContentHash = 7;

	CHECK(base == Graphite::DiffSelectionContext{base});

	Graphite::DiffSelectionContext other = base;
	other.ContentHash = 8;
	CHECK_FALSE(other == base);

	other = base;
	other.Mode = Graphite::DiffViewMode::SideBySide;
	CHECK_FALSE(other == base);

	other = base;
	other.Scope = Graphite::DiffScope::WholeFile;
	CHECK_FALSE(other == base);

	other = base;
	other.Side = DiffSide::Old;
	CHECK_FALSE(other == base);

	other = base;
	other.Path = "b.txt";
	CHECK_FALSE(other == base);
}
