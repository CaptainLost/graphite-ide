#include <doctest/doctest.h>

#include "Core/ImGui/CaptionLayout.h"

using Core::CaptionLayout;
using Core::LayOutCaption;

namespace {
	constexpr float kBarWidth = 800.0f;
	constexpr float kBarHeight = 32.0f;
	constexpr float kButtonWidth = 46.0f;
	constexpr float kVersionWidth = 100.0f;
	constexpr float kVersionGap = 12.0f;
	constexpr float kContentLeftOfMenus = 200.0f;
	constexpr float kContentLeftAtEdge = 0.0f;
}

TEST_CASE("The three buttons sit flush to the right edge, in window order")
{
	const CaptionLayout layout =
		LayOutCaption(kBarWidth, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, kContentLeftOfMenus);

	CHECK(layout.Close.X + layout.Close.Width == doctest::Approx(kBarWidth));
	CHECK(layout.Maximize.X + layout.Maximize.Width == doctest::Approx(layout.Close.X));
	CHECK(layout.Minimize.X + layout.Minimize.Width == doctest::Approx(layout.Maximize.X));

	CHECK(layout.Minimize.Width == doctest::Approx(kButtonWidth));
	CHECK(layout.Minimize.Height == doctest::Approx(kBarHeight));
	CHECK(layout.Minimize.Y == doctest::Approx(0.0f));
}

TEST_CASE("The version label sits left of the buttons, one gap away")
{
	const CaptionLayout layout =
		LayOutCaption(kBarWidth, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, kContentLeftOfMenus);

	CHECK(layout.VersionVisible);
	CHECK(layout.Version.Width == doctest::Approx(kVersionWidth));
	CHECK(layout.Version.X + layout.Version.Width + kVersionGap ==
		  doctest::Approx(layout.Minimize.X));
}

TEST_CASE("The version label is dropped rather than drawn over the menus")
{
	const float menusRightEdge = 700.0f;
	const CaptionLayout layout =
		LayOutCaption(kBarWidth, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, menusRightEdge);

	CHECK_FALSE(layout.VersionVisible);
	CHECK(layout.Version.X < menusRightEdge);
	CHECK(layout.Close.X + layout.Close.Width == doctest::Approx(kBarWidth));
}

TEST_CASE("A bar too narrow for the buttons keeps close flush right and loses minimize first")
{
	const float narrowBarWidth = 100.0f;
	const CaptionLayout layout =
		LayOutCaption(narrowBarWidth, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, kContentLeftAtEdge);

	CHECK(layout.Close.X + layout.Close.Width == doctest::Approx(narrowBarWidth));
	CHECK(layout.Maximize.X + layout.Maximize.Width == doctest::Approx(layout.Close.X));
	CHECK(layout.Minimize.X < 0.0f);
	CHECK_FALSE(layout.VersionVisible);
}

TEST_CASE("A bar narrower than one button still leaves close reachable at the right edge")
{
	const float barNarrowerThanButton = 30.0f;
	const CaptionLayout layout =
		LayOutCaption(barNarrowerThanButton, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, kContentLeftAtEdge);

	CHECK(layout.Close.X + layout.Close.Width == doctest::Approx(barNarrowerThanButton));
	CHECK(layout.Close.X < 0.0f);
}
