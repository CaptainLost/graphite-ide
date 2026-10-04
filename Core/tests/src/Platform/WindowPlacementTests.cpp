#include <doctest/doctest.h>

#include "Core/Platform/WindowPlacement.h"

#include <vector>

using Core::FitToMonitors;
using Core::ScreenRect;
using Core::WindowPlacement;

namespace {
	// A 1920x1080 primary with a 40-pixel taskbar at the bottom, and a second monitor to its right.
	const ScreenRect kPrimary{0, 0, 1920, 1040};
	const ScreenRect kSecond{1920, 0, 3840, 1040};
}

TEST_CASE("A default window larger than the work area shrinks to 85% of it and is centred")
{
	// 85% of 1920x1040 is 1632x884.
	const WindowPlacement placement = Core::DefaultPlacement(2400, 1350, kPrimary);

	CHECK(placement == WindowPlacement{144, 78, 1632, 884, false});
}

TEST_CASE("A default window that fits is left its size and centred")
{
	const WindowPlacement placement = Core::DefaultPlacement(1600, 800, kPrimary);

	CHECK(placement == WindowPlacement{160, 120, 1600, 800, false});
}

TEST_CASE("A default window is centred in a work area that does not start at the origin")
{
	// A taskbar docked on the left and a monitor to the left of the primary one.
	const ScreenRect offset{-1000, 60, 600, 1060};
	const WindowPlacement placement = Core::DefaultPlacement(1600, 900, offset);

	CHECK(placement.Width == 1360);
	CHECK(placement.Height == 850);
	CHECK(placement.X == -1000 + (1600 - 1360) / 2);
	CHECK(placement.Y == 60 + (1000 - 850) / 2);
}

TEST_CASE("A default window on a tiny work area shrinks with it and stays inside it")
{
	const ScreenRect tiny{10, 20, 110, 80};
	const WindowPlacement placement = Core::DefaultPlacement(1600, 900, tiny);

	CHECK(placement == WindowPlacement{10 + (100 - 85) / 2, 20 + (60 - 51) / 2, 85, 51, false});
	CHECK(placement.X >= tiny.Left);
	CHECK(placement.X + placement.Width <= tiny.Right);
	CHECK(placement.Y + placement.Height <= tiny.Bottom);
}

TEST_CASE("A window inside the primary monitor stays where it was")
{
	const std::vector<ScreenRect> areas{kPrimary, kSecond};
	const WindowPlacement placement{100, 100, 1600, 900, false};

	CHECK(FitToMonitors(placement, areas, kPrimary) == placement);
}

TEST_CASE("A window on the second monitor stays there while that monitor is attached")
{
	const std::vector<ScreenRect> areas{kPrimary, kSecond};
	const WindowPlacement placement{2000, 50, 1600, 900, true};

	CHECK(FitToMonitors(placement, areas, kPrimary) == placement);
}

TEST_CASE("A window straddling two monitors stays where it was")
{
	const std::vector<ScreenRect> areas{kPrimary, kSecond};
	const WindowPlacement placement{1500, 100, 1000, 700, false};

	CHECK(FitToMonitors(placement, areas, kPrimary) == placement);
}

TEST_CASE("A window left on a detached monitor is centred on the primary at its own size")
{
	const std::vector<ScreenRect> areas{kPrimary};
	const WindowPlacement placement{2000, 50, 1600, 900, true};

	const WindowPlacement fitted = FitToMonitors(placement, areas, kPrimary);

	CHECK(fitted == WindowPlacement{160, 70, 1600, 900, true});
}

TEST_CASE("A window larger than the primary is shrunk to fit it when it has to move")
{
	const std::vector<ScreenRect> areas{kPrimary};
	const WindowPlacement placement{-5000, -5000, 2560, 1440, false};

	CHECK(FitToMonitors(placement, areas, kPrimary) == WindowPlacement{0, 0, 1920, 1040, false});
}

TEST_CASE("A window with only a sliver on screen is brought back, since nobody could grab it")
{
	const std::vector<ScreenRect> areas{kPrimary};
	const WindowPlacement placement{1910, 100, 800, 600, false};

	const WindowPlacement fitted = FitToMonitors(placement, areas, kPrimary);

	CHECK(fitted == WindowPlacement{560, 220, 800, 600, false});
}

TEST_CASE("With no monitors reported the window is placed on the primary")
{
	const WindowPlacement placement{100, 100, 800, 600, false};

	CHECK(FitToMonitors(placement, {}, kPrimary) == WindowPlacement{560, 220, 800, 600, false});
}
