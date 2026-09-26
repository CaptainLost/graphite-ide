#include <doctest/doctest.h>

#include "Git/ChangeOverview.h"

#include <array>
#include <cstddef>

using Graphite::BuildChangeOverview;
using Graphite::ChangeHunk;
using Graphite::ChangeMarkers;
using Graphite::HunkKind;
using Graphite::OverviewMark;

namespace {

	// Ten lines of equal height, the way an unwrapped file with no lenses lays out.
	float EvenTenths(std::size_t line)
	{
		return static_cast<float>(line) / 10.0f;
	}

	ChangeHunk Hunk(HunkKind kind, bool staged, std::size_t first, std::size_t count)
	{
		return ChangeHunk{.Kind = kind, .Staged = staged, .FirstLine = first, .LineCount = count, .MarkedCount = count};
	}
}

TEST_CASE("No hunks put nothing on the scrollbar")
{
	CHECK(BuildChangeOverview(ChangeMarkers{}, EvenTenths, 0.01f).empty());
}

TEST_CASE("A hunk spans the scrollbar from its first line to the line after its last")
{
	ChangeMarkers markers;
	markers.Hunks.push_back(Hunk(HunkKind::Modified, false, 2, 3));

	const auto marks = BuildChangeOverview(markers, EvenTenths, 0.01f);

	REQUIRE(marks.size() == 1);
	CHECK(marks[0].From == doctest::Approx(0.2f));
	CHECK(marks[0].To == doctest::Approx(0.5f));
	CHECK(marks[0].Kind == HunkKind::Modified);
	CHECK_FALSE(marks[0].Staged);
}

TEST_CASE("A mark is never shorter than the minimum")
{
	ChangeMarkers markers;
	markers.Hunks.push_back(Hunk(HunkKind::Added, false, 2, 1));

	const auto marks = BuildChangeOverview(
		markers, [](std::size_t line) { return static_cast<float>(line) / 1000.0f; }, 0.01f);

	REQUIRE(marks.size() == 1);
	CHECK(marks[0].From == doctest::Approx(0.002f));
	CHECK(marks[0].To == doctest::Approx(0.012f));
}

TEST_CASE("A deletion is a minimum-height mark at its boundary")
{
	ChangeMarkers markers;
	markers.Hunks.push_back(Hunk(HunkKind::Deleted, false, 4, 0));

	const auto marks = BuildChangeOverview(markers, EvenTenths, 0.01f);

	REQUIRE(marks.size() == 1);
	CHECK(marks[0].Kind == HunkKind::Deleted);
	CHECK(marks[0].From == doctest::Approx(0.4f));
	CHECK(marks[0].To == doctest::Approx(0.41f));
}

TEST_CASE("A mark at the very end is kept inside the scrollbar")
{
	ChangeMarkers markers;
	markers.Hunks.push_back(Hunk(HunkKind::Deleted, false, 10, 0));

	const auto marks = BuildChangeOverview(markers, EvenTenths, 0.01f);

	REQUIRE(marks.size() == 1);
	CHECK(marks[0].From == doctest::Approx(0.99f));
	CHECK(marks[0].To == doctest::Approx(1.0f));
}

TEST_CASE("Staged marks come first so unstaged ones are drawn over them")
{
	ChangeMarkers markers;
	markers.Hunks.push_back(Hunk(HunkKind::Added, false, 0, 1));
	markers.Hunks.push_back(Hunk(HunkKind::Modified, true, 5, 2));
	markers.Hunks.push_back(Hunk(HunkKind::Modified, false, 7, 1));

	const auto marks = BuildChangeOverview(markers, EvenTenths, 0.01f);

	REQUIRE(marks.size() == 3);
	CHECK(marks[0].Staged);
	CHECK_FALSE(marks[1].Staged);
	CHECK_FALSE(marks[2].Staged);
	CHECK(marks[1].From == doctest::Approx(0.0f));
	CHECK(marks[2].From == doctest::Approx(0.7f));
}

TEST_CASE("Positions come from the layout rather than the line number")
{
	// A wrapped line 1 takes four rows' worth of the document.
	static constexpr std::array<float, 5> kTops{0.0f, 0.1f, 0.5f, 0.6f, 1.0f};

	ChangeMarkers markers;
	markers.Hunks.push_back(Hunk(HunkKind::Modified, false, 1, 1));

	const auto marks = BuildChangeOverview(markers, [](std::size_t line) { return kTops[line]; }, 0.01f);

	REQUIRE(marks.size() == 1);
	CHECK(marks[0].From == doctest::Approx(0.1f));
	CHECK(marks[0].To == doctest::Approx(0.5f));
}

TEST_CASE("A stale hunk reaching past the end is pulled back inside the track")
{
	ChangeMarkers markers;
	markers.Hunks.push_back(Hunk(HunkKind::Added, false, 9, 5));

	const auto marks = BuildChangeOverview(markers, EvenTenths, 0.01f);

	REQUIRE(marks.size() == 1);
	CHECK(marks[0].To == doctest::Approx(1.0f));
	CHECK(marks[0].From >= 0.0f);
	CHECK(marks[0].From <= marks[0].To);
}
