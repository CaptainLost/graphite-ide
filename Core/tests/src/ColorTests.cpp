#include <doctest/doctest.h>

#include "Core/Base/Color.h"
#include "Core/Gui/Theme.h"

using Core::Color;
using Core::PackBgr;
using Core::Theme;

TEST_CASE("The channels land in the order the system reads them, blue highest")
{
    CHECK(PackBgr(Color{1.0f, 0.0f, 0.0f, 1.0f}) == 0x0000FFu);
    CHECK(PackBgr(Color{0.0f, 1.0f, 0.0f, 1.0f}) == 0x00FF00u);
    CHECK(PackBgr(Color{0.0f, 0.0f, 1.0f, 1.0f}) == 0xFF0000u);
}

TEST_CASE("Alpha is dropped, because the border it colours has none")
{
    CHECK(PackBgr(Color{1.0f, 1.0f, 1.0f, 0.0f}) == 0xFFFFFFu);
    CHECK(PackBgr(Color{1.0f, 1.0f, 1.0f, 1.0f}) == 0xFFFFFFu);
}

TEST_CASE("Channels round to nearest rather than truncating")
{
    // 0.247 is 62.985 of 255: truncation would say 62, which is a different colour from
    // the one the theme paints the same float with.
    CHECK(PackBgr(Color{0.247f, 0.0f, 0.0f, 1.0f}) == 63u);
    CHECK(PackBgr(Color{0.5f, 0.0f, 0.0f, 1.0f}) == 128u);
}

TEST_CASE("Channels outside the unit range clamp to the end they passed")
{
    CHECK(PackBgr(Color{-1.0f, 2.0f, 0.0f, 1.0f}) == 0x00FF00u);
}

TEST_CASE("The window border is the theme's border colour, unchanged")
{
    // 0x3F3834 is the RGB(52, 56, 63) that DWM used to be handed as a literal in
    // the frame, back when the theme and the frame each spelled this colour out.
    // The frame now packs whatever the theme says, so this is what pins the two together
    // — nothing else in the process can observe the colour a window border was given.
    CHECK(PackBgr(Theme::BorderColor()) == 0x3F3834u);
}
