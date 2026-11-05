#include <doctest/doctest.h>

#include "Git/Compare/HeaderShares.h"

#include <array>

using namespace Graphite;

namespace {

	HeaderWidths Header(float row)
	{
		HeaderWidths widths;
		widths.Row = row;
		widths.Taken = 20.0f;
		widths.VsButton = 300.0f;
		widths.Switch = 80.0f;
		widths.Operation = 0.0f;
		widths.Prefix = 40.0f;
		widths.Title = 160.0f;
		widths.Fields = 400.0f;
		widths.Spacing = 8.0f;
		return widths;
	}
}

TEST_CASE("Git window: with room to spare the header keeps every width")
{
	HeaderWidths widths = Header(2000.0f);
	widths.VsButton = 100.0f;

	const HeaderShares shares = ShareHeader(widths);

	CHECK(shares.VsButton == 100.0f);
	CHECK(shares.NameRoom == 200.0f);
}

TEST_CASE("Git window: the vs button takes at most its share of the row")
{
	const HeaderShares shares = ShareHeader(Header(500.0f));

	CHECK(shares.VsButton == doctest::Approx(500.0f * kVsButtonShare));
}

TEST_CASE("Git window: at a narrow width no share goes below zero")
{
	constexpr std::array<float, 3> kRows{0.0f, 30.0f, 120.0f};

	for (const float row : kRows)
	{
		const HeaderShares shares = ShareHeader(Header(row));

		CHECK(shares.VsButton >= 0.0f);
		CHECK(shares.NameRoom >= 0.0f);
	}

	CHECK(ShareHeader(Header(120.0f)).NameRoom == 0.0f);
}

TEST_CASE("Git window: the name and the fields shrink in proportion short of room")
{
	HeaderWidths widths = Header(700.0f);
	widths.VsButton = 100.0f;

	const HeaderShares shares = ShareHeader(widths);

	CHECK(shares.NameRoom < widths.Prefix + widths.Title);
	CHECK(shares.NameRoom > 0.0f);
}

TEST_CASE("Git window: the filter fields give up the path first and never go below zero")
{
	const FilterFieldWidths roomy = SplitFilterFields(600.0f, 150.0f, 400.0f, 8.0f);
	CHECK(roomy.Path == 150.0f);
	CHECK(roomy.Words == doctest::Approx(600.0f - 150.0f - 8.0f));

	const FilterFieldWidths tight = SplitFilterFields(100.0f, 150.0f, 400.0f, 8.0f);
	CHECK(tight.Path < 150.0f);
	CHECK(tight.Words >= 0.0f);

	const FilterFieldWidths none = SplitFilterFields(-20.0f, 150.0f, 400.0f, 8.0f);
	CHECK(none.Path == 0.0f);
	CHECK(none.Words == 0.0f);
}
