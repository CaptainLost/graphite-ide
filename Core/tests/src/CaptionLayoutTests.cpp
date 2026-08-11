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
    // Menus reach x=700, and the label would have to start at 630 to fit.
    const CaptionLayout layout =
        LayOutCaption(kBarWidth, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, 700.0f);

    CHECK_FALSE(layout.versionVisible);
    CHECK(layout.close.x + layout.close.width == doctest::Approx(kBarWidth));
}

TEST_CASE("A bar too narrow for the buttons still reports them, clamped to the left edge")
{
    const CaptionLayout layout =
        LayOutCaption(100.0f, kBarHeight, kButtonWidth, kVersionWidth, kVersionGap, 0.0f);

    // A window narrower than its own buttons cannot show all three, and keeping them on
    // screen and in order beats hiding the one that closes the window.
    CHECK(layout.minimize.x == doctest::Approx(0.0f));
    CHECK(layout.maximize.x == doctest::Approx(kButtonWidth));
    CHECK(layout.close.x == doctest::Approx(2.0f * kButtonWidth));
    CHECK_FALSE(layout.versionVisible);
}
