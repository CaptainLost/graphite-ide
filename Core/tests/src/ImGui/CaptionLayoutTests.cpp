#include <doctest/doctest.h>

#include "Core/ImGui/CaptionLayout.h"

#include <initializer_list>

using Core::CaptionLayout;
using Core::CaptionTitleSpacing;
using Core::CaptionTitleWidths;
using Core::LayOutCaption;

namespace {
	constexpr float kBarWidth = 800.0f;
	constexpr float kBarHeight = 28.0f;
	constexpr float kButtonWidth = 42.0f;
	constexpr float kContentLeftOfMenus = 200.0f;
	constexpr float kContentLeftAtEdge = 0.0f;

	constexpr CaptionTitleSpacing kSpacing{.Margin = 10.0f, .DividerWidth = 1.0f, .ButtonsGap = 12.0f};
	constexpr CaptionTitleWidths kFolder{.Folder = 100.0f};
	constexpr CaptionTitleWidths kNoFolder{};

	CaptionLayout LayOut(float barWidth, float contentLeft, const CaptionTitleWidths& widths)
	{
		return LayOutCaption(barWidth, kBarHeight, kButtonWidth, contentLeft, widths, kSpacing);
	}
}

TEST_CASE("The three buttons sit flush to the right edge, in window order")
{
	const CaptionLayout layout = LayOut(kBarWidth, kContentLeftOfMenus, kFolder);

	CHECK(layout.Close.X + layout.Close.Width == doctest::Approx(kBarWidth));
	CHECK(layout.Maximize.X + layout.Maximize.Width == doctest::Approx(layout.Close.X));
	CHECK(layout.Minimize.X + layout.Minimize.Width == doctest::Approx(layout.Maximize.X));

	CHECK(layout.Minimize.Width == doctest::Approx(kButtonWidth));
	CHECK(layout.Minimize.Height == doctest::Approx(kBarHeight));
	CHECK(layout.Minimize.Y == doctest::Approx(0.0f));
}

TEST_CASE("The divider and the folder follow the menus in that order, spaced apart")
{
	const CaptionLayout layout = LayOut(kBarWidth, kContentLeftOfMenus, kFolder);

	REQUIRE(layout.DividerVisible);
	REQUIRE(layout.FolderVisible);

	CHECK(layout.Divider.X == doctest::Approx(kContentLeftOfMenus + kSpacing.Margin));
	CHECK(layout.Divider.Width == doctest::Approx(kSpacing.DividerWidth));
	CHECK(layout.Folder.X == doctest::Approx(layout.Divider.X + layout.Divider.Width + kSpacing.Margin));
	CHECK(layout.Folder.Width == doctest::Approx(kFolder.Folder));

	for (const Core::CaptionRect* rectPointer : {&layout.Divider, &layout.Folder})
	{
		const Core::CaptionRect& rect = *rectPointer;
		CHECK(rect.Y == doctest::Approx(0.0f));
		CHECK(rect.Height == doctest::Approx(kBarHeight));
	}
}

TEST_CASE("With no folder nothing follows the menus")
{
	const CaptionLayout layout = LayOut(kBarWidth, kContentLeftOfMenus, kNoFolder);

	CHECK_FALSE(layout.DividerVisible);
	CHECK_FALSE(layout.FolderVisible);
}

TEST_CASE("The folder fits when it ends exactly one gap before the buttons")
{
	const CaptionLayout whole = LayOut(kBarWidth, kContentLeftOfMenus, kFolder);
	const float folderEnd = whole.Folder.X + whole.Folder.Width;
	const float exactBar = folderEnd + kSpacing.ButtonsGap + 3.0f * kButtonWidth;

	CHECK(LayOut(exactBar, kContentLeftOfMenus, kFolder).FolderVisible);
}

TEST_CASE("The folder is dropped with the divider rather than overlap the buttons")
{
	const CaptionLayout whole = LayOut(kBarWidth, kContentLeftOfMenus, kFolder);
	const float folderEnd = whole.Folder.X + whole.Folder.Width;
	const float barJustTooNarrow = folderEnd + kSpacing.ButtonsGap + 3.0f * kButtonWidth - 1.0f;
	const CaptionLayout layout = LayOut(barJustTooNarrow, kContentLeftOfMenus, kFolder);

	CHECK_FALSE(layout.FolderVisible);
	CHECK_FALSE(layout.DividerVisible);
	CHECK(layout.Close.X + layout.Close.Width == doctest::Approx(barJustTooNarrow));
}

TEST_CASE("The labels are dropped rather than drawn over the menus")
{
	const float menusRightEdge = 700.0f;
	const CaptionLayout layout = LayOut(kBarWidth, menusRightEdge, kFolder);

	CHECK_FALSE(layout.FolderVisible);
	CHECK(layout.Close.X + layout.Close.Width == doctest::Approx(kBarWidth));
}

TEST_CASE("A bar too narrow for the buttons keeps close flush right and loses minimize first")
{
	const float narrowBarWidth = 100.0f;
	const CaptionLayout layout = LayOut(narrowBarWidth, kContentLeftAtEdge, kFolder);

	CHECK(layout.Close.X + layout.Close.Width == doctest::Approx(narrowBarWidth));
	CHECK(layout.Maximize.X + layout.Maximize.Width == doctest::Approx(layout.Close.X));
	CHECK(layout.Minimize.X < 0.0f);
	CHECK_FALSE(layout.FolderVisible);
}

TEST_CASE("A bar narrower than one button still leaves close reachable at the right edge")
{
	const float barNarrowerThanButton = 30.0f;
	const CaptionLayout layout = LayOut(barNarrowerThanButton, kContentLeftAtEdge, kFolder);

	CHECK(layout.Close.X + layout.Close.Width == doctest::Approx(barNarrowerThanButton));
	CHECK(layout.Close.X < 0.0f);
}
