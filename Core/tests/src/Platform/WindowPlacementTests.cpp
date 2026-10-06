#include <doctest/doctest.h>

#include "Core/Platform/WindowPlacement.h"

#include <vector>

using Core::FitToMonitors;
using Core::ScreenRect;
using Core::WindowPlacement;

namespace {
	constexpr int kMonitorWidth = 1920;
	constexpr int kMonitorHeight = 1080;
	constexpr int kBottomTaskbarHeight = 40;

	const ScreenRect kPrimaryWorkArea{.Left = 0, .Top = 0, .Right = kMonitorWidth, .Bottom = kMonitorHeight - kBottomTaskbarHeight};
	const ScreenRect kSecondMonitorWorkAreaToTheRight{.Left = kMonitorWidth, .Top = 0, .Right = 2 * kMonitorWidth, .Bottom = kMonitorHeight - kBottomTaskbarHeight};
}

TEST_CASE("A default window larger than the work area shrinks to 85% of it to clear the edges and is centred")
{
	const int preferredWidth = 2400;
	const int preferredHeight = 1350;
	const int eightyFivePercentOfWorkAreaWidth = 1632;
	const int eightyFivePercentOfWorkAreaHeight = 884;

	const WindowPlacement placement = Core::DefaultPlacement(preferredWidth, preferredHeight, kPrimaryWorkArea);

	CHECK(placement == WindowPlacement{.X = 144, .Y = 78, .Width = eightyFivePercentOfWorkAreaWidth, .Height = eightyFivePercentOfWorkAreaHeight, .Maximized = false});
}

TEST_CASE("A default window that fits is left its size and centred")
{
	const int preferredWidth = 1600;
	const int preferredHeight = 800;

	const WindowPlacement placement = Core::DefaultPlacement(preferredWidth, preferredHeight, kPrimaryWorkArea);

	CHECK(placement == WindowPlacement{.X = 160, .Y = 120, .Width = 1600, .Height = 800, .Maximized = false});
}

TEST_CASE("A default window is centred in a work area that does not start at the origin")
{
	const ScreenRect workAreaAwayFromOrigin{.Left = -1000, .Top = 60, .Right = 600, .Bottom = 1060};
	const int preferredWidth = 1600;
	const int preferredHeight = 900;

	const WindowPlacement placement = Core::DefaultPlacement(preferredWidth, preferredHeight, workAreaAwayFromOrigin);

	CHECK(placement.Width == 1360);
	CHECK(placement.Height == 850);
	CHECK(placement.X == -1000 + (1600 - 1360) / 2);
	CHECK(placement.Y == 60 + (1000 - 850) / 2);
}

TEST_CASE("A default window on a tiny work area shrinks with it and stays inside it")
{
	const ScreenRect tinyWorkArea{.Left = 10, .Top = 20, .Right = 110, .Bottom = 80};
	const int preferredWidth = 1600;
	const int preferredHeight = 900;

	const WindowPlacement placement = Core::DefaultPlacement(preferredWidth, preferredHeight, tinyWorkArea);

	CHECK(placement == WindowPlacement{.X = 10 + (100 - 85) / 2, .Y = 20 + (60 - 51) / 2, .Width = 85, .Height = 51, .Maximized = false});
	CHECK(placement.X >= tinyWorkArea.Left);
	CHECK(placement.X + placement.Width <= tinyWorkArea.Right);
	CHECK(placement.Y + placement.Height <= tinyWorkArea.Bottom);
}

TEST_CASE("A window inside the primary monitor stays where it was")
{
	const std::vector<ScreenRect> workAreas{kPrimaryWorkArea, kSecondMonitorWorkAreaToTheRight};
	const WindowPlacement placement{.X = 100, .Y = 100, .Width = 1600, .Height = 900, .Maximized = false};

	CHECK(FitToMonitors(placement, workAreas, kPrimaryWorkArea) == placement);
}

TEST_CASE("A window on the second monitor stays there while that monitor is attached")
{
	const std::vector<ScreenRect> workAreas{kPrimaryWorkArea, kSecondMonitorWorkAreaToTheRight};
	const WindowPlacement placement{.X = 2000, .Y = 50, .Width = 1600, .Height = 900, .Maximized = true};

	CHECK(FitToMonitors(placement, workAreas, kPrimaryWorkArea) == placement);
}

TEST_CASE("A window straddling two monitors stays where it was")
{
	const std::vector<ScreenRect> workAreas{kPrimaryWorkArea, kSecondMonitorWorkAreaToTheRight};
	const WindowPlacement placement{.X = 1500, .Y = 100, .Width = 1000, .Height = 700, .Maximized = false};

	CHECK(FitToMonitors(placement, workAreas, kPrimaryWorkArea) == placement);
}

TEST_CASE("A window left on a detached monitor is centred on the primary at its own size")
{
	const std::vector<ScreenRect> workAreas{kPrimaryWorkArea};
	const WindowPlacement placement{.X = 2000, .Y = 50, .Width = 1600, .Height = 900, .Maximized = true};

	const WindowPlacement fitted = FitToMonitors(placement, workAreas, kPrimaryWorkArea);

	CHECK(fitted == WindowPlacement{.X = 160, .Y = 70, .Width = 1600, .Height = 900, .Maximized = true});
}

TEST_CASE("A window larger than the primary is shrunk to fit it when it has to move")
{
	const std::vector<ScreenRect> workAreas{kPrimaryWorkArea};
	const WindowPlacement placement{.X = -5000, .Y = -5000, .Width = 2560, .Height = 1440, .Maximized = false};

	CHECK(FitToMonitors(placement, workAreas, kPrimaryWorkArea) == WindowPlacement{.X = 0, .Y = 0, .Width = 1920, .Height = 1040, .Maximized = false});
}

TEST_CASE("A window with only a sliver on screen is brought back, since nobody could grab it")
{
	const std::vector<ScreenRect> workAreas{kPrimaryWorkArea};
	const WindowPlacement placement{.X = 1910, .Y = 100, .Width = 800, .Height = 600, .Maximized = false};

	const WindowPlacement fitted = FitToMonitors(placement, workAreas, kPrimaryWorkArea);

	CHECK(fitted == WindowPlacement{.X = 560, .Y = 220, .Width = 800, .Height = 600, .Maximized = false});
}

TEST_CASE("With no monitors reported the window is placed on the primary")
{
	const std::vector<ScreenRect> noWorkAreas;
	const WindowPlacement placement{.X = 100, .Y = 100, .Width = 800, .Height = 600, .Maximized = false};

	CHECK(FitToMonitors(placement, noWorkAreas, kPrimaryWorkArea) == WindowPlacement{.X = 560, .Y = 220, .Width = 800, .Height = 600, .Maximized = false});
}
