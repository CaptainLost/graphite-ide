#include <doctest/doctest.h>

#include "Git/DiffRows.h"

#include <string>
#include <vector>

using Graphite::DiffMeasurement;
using Graphite::MeasurementOf;

namespace {

	Core::DiffLine Line(int oldLine, int newLine, std::string text)
	{
		Core::DiffLine line;
		line.Kind = Core::DiffLineKind::Context;
		line.OldLine = oldLine;
		line.NewLine = newLine;
		line.Text = std::move(text);
		return line;
	}

	Core::FileDiff FileWith(std::string path, std::vector<std::vector<Core::DiffLine>> hunks)
	{
		Core::FileDiff file;
		file.OldPath = path;
		file.NewPath = path;

		for (std::vector<Core::DiffLine>& lines : hunks)
		{
			Core::Hunk hunk;
			hunk.Header = "@@ @@\n";
			hunk.Lines = std::move(lines);
			file.Hunks.push_back(std::move(hunk));
		}

		return file;
	}

	Core::FileDiff Simple(std::string path)
	{
		return FileWith(std::move(path), {{Line(1, 1, "one"), Line(2, 2, "two")}});
	}
}

TEST_CASE("The same diff measures the same, so a kept width survives scrolling")
{
	CHECK(MeasurementOf(Simple("a.cpp"), false) == MeasurementOf(Simple("a.cpp"), false));
}

TEST_CASE("A different file measures differently")
{
	CHECK_FALSE(MeasurementOf(Simple("a.cpp"), false) == MeasurementOf(Simple("b.cpp"), false));
}

TEST_CASE("The same path in a different commit measures differently")
{
	// The case a path-keyed reset misses: one DiffView serves every commit, so the path is
	// unchanged while the lines are another commit's entirely. Keeping the width here is what
	// leaves the reader scrolled into blank space.
	const Core::FileDiff before = FileWith("a.cpp", {{Line(1, 1, "one"), Line(2, 2, "two")}});
	const Core::FileDiff after = FileWith("a.cpp", {{Line(1, 1, "one")}});

	CHECK_FALSE(MeasurementOf(before, false) == MeasurementOf(after, false));
}

TEST_CASE("An edit at the bottom measures differently from one of the same shape at the top")
{
	// Equal in path, hunk count and line count — only where the lines sit in the file differs,
	// which is what the last line's numbers are read for.
	const Core::FileDiff top = FileWith("a.cpp", {{Line(1, 1, "one"), Line(2, 2, "two")}});
	const Core::FileDiff bottom = FileWith("a.cpp", {{Line(90, 90, "one"), Line(91, 91, "two")}});

	CHECK_FALSE(MeasurementOf(top, false) == MeasurementOf(bottom, false));
}

TEST_CASE("Splitting the same lines across two hunks measures differently")
{
	const Core::FileDiff one = FileWith("a.cpp", {{Line(1, 1, "one"), Line(2, 2, "two")}});
	const Core::FileDiff two = FileWith("a.cpp", {{Line(1, 1, "one")}, {Line(2, 2, "two")}});

	CHECK_FALSE(MeasurementOf(one, false) == MeasurementOf(two, false));
}

TEST_CASE("Switching to the whole file measures differently from the changes alone")
{
	// The rows are a different set even though the diff is the same, and the whole file is
	// where the long lines a hunk never showed come from.
	const Core::FileDiff file = Simple("a.cpp");

	CHECK_FALSE(MeasurementOf(file, true) == MeasurementOf(file, false));
}

TEST_CASE("A deleted file measures by the path that names it")
{
	Core::FileDiff deleted = Simple("a.cpp");
	deleted.NewPath.clear();
	deleted.Change = Core::FileChange::Deleted;

	Core::FileDiff other = Simple("b.cpp");
	other.NewPath.clear();
	other.Change = Core::FileChange::Deleted;

	// Both have an empty new path; keying on that alone would call them the same file.
	CHECK_FALSE(MeasurementOf(deleted, false) == MeasurementOf(other, false));
	CHECK(MeasurementOf(deleted, false).Path == "a.cpp");
}

TEST_CASE("A file with no hunks measures without reading a line that is not there")
{
	Core::FileDiff empty;
	empty.OldPath = "a.cpp";
	empty.NewPath = "a.cpp";

	const DiffMeasurement measurement = MeasurementOf(empty, false);

	CHECK(measurement.Hunks == 0);
	CHECK(measurement.Lines == 0);
	CHECK(measurement.LastOldLine == 0);
	CHECK(measurement.LastNewLine == 0);
}

TEST_CASE("A hunk with no lines measures without reading past its end")
{
	Core::FileDiff file;
	file.OldPath = "a.cpp";
	file.NewPath = "a.cpp";
	file.Hunks.push_back(Core::Hunk{});

	const DiffMeasurement measurement = MeasurementOf(file, false);

	CHECK(measurement.Hunks == 1);
	CHECK(measurement.Lines == 0);
}
