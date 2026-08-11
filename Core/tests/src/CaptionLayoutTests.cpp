#include <doctest/doctest.h>

#include "Core/Gui/CaptionLayout.h"

using core::CaptionLayout;
using core::LayOutCaption;

namespace
{
    constexpr float kBarWidth = 800.0f;
    constexpr float kBarHeight = 32.0f;
    constexpr float kButtonWidth = 46.0f;
    constexpr float kVersionWidth = 100.0f;
    constexpr float kVersionGap = 12.0f;
}

TEST_CASE("The three buttons sit flush to the right edge, in window order")
{
    const CaptionLayout layout =
        LayOutCaption(kBarWidth, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, 200.0f);

    CHECK(layout.close.x + layout.close.width == doctest::Approx(kBarWidth));
    CHECK(layout.maximize.x + layout.maximize.width == doctest::Approx(layout.close.x));
    CHECK(layout.minimize.x + layout.minimize.width == doctest::Approx(layout.maximize.x));

    CHECK(layout.minimize.width == doctest::Approx(kButtonWidth));
    CHECK(layout.minimize.height == doctest::Approx(kBarHeight));
    CHECK(layout.minimize.y == doctest::Approx(0.0f));
}

TEST_CASE("The version label sits left of the buttons, one gap away")
{
    const CaptionLayout layout =
        LayOutCaption(kBarWidth, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, 200.0f);

    CHECK(layout.versionVisible);
    CHECK(layout.version.width == doctest::Approx(kVersionWidth));
    CHECK(layout.version.x + layout.version.width + kVersionGap ==
          doctest::Approx(layout.minimize.x));
}

TEST_CASE("The version label is dropped rather than drawn over the menus")
{
    // Menus reach x=700, and the label would have to start at 550 to fit.
    const CaptionLayout layout =
        LayOutCaption(kBarWidth, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, 700.0f);

    CHECK_FALSE(layout.versionVisible);
    CHECK(layout.close.x + layout.close.width == doctest::Approx(kBarWidth));
}

TEST_CASE("A bar too narrow for the buttons keeps close flush right and loses minimize first")
{
    const CaptionLayout layout =
        LayOutCaption(100.0f, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, 0.0f);

    // A window narrower than its own buttons cannot show all three, and the one that
    // closes the window is the one that has to survive — so the row is measured from the
    // right edge and minimize is what runs off to the left.
    CHECK(layout.close.x + layout.close.width == doctest::Approx(100.0f));
    CHECK(layout.maximize.x + layout.maximize.width == doctest::Approx(layout.close.x));
    CHECK(layout.minimize.x < 0.0f);
    CHECK_FALSE(layout.versionVisible);
}

TEST_CASE("A bar narrower than one button still leaves close reachable at the right edge")
{
    const CaptionLayout layout =
        LayOutCaption(30.0f, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, 0.0f);

    // Even here close reaches the right edge: part of it is off screen to the left, but
    // the part under the corner where it always sits is the part that stays.
    CHECK(layout.close.x + layout.close.width == doctest::Approx(30.0f));
    CHECK(layout.close.x < 0.0f);
}
