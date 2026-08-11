#include <doctest/doctest.h>

#include "Core/Platform/Caption.h"

using core::CaptionHit;
using core::CaptionMetrics;
using core::Classify;

namespace
{
    constexpr float kWidth = 800.0f;
    constexpr float kHeight = 600.0f;

    /// The menus reach x=200 and the window buttons start at x=700; everything between
    /// them is caption the application left free.
    constexpr float kDragBegin = 200.0f;
    constexpr float kDragEnd = 700.0f;

    CaptionMetrics Metrics()
    {
        CaptionMetrics metrics;
        metrics.height = 32.0f;
        metrics.resizeBorder = 8.0f;
        metrics.dragBegin = kDragBegin;
        metrics.dragEnd = kDragEnd;
        return metrics;
    }
}

TEST_CASE("Classify finds the resize edges of a restored window")
{
    const CaptionMetrics metrics = Metrics();

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 2.0f, false) == CaptionHit::Top);
    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 598.0f, false) == CaptionHit::Bottom);
    CHECK(Classify(metrics, kWidth, kHeight, 2.0f, 300.0f, false) == CaptionHit::Left);
    CHECK(Classify(metrics, kWidth, kHeight, 798.0f, 300.0f, false) == CaptionHit::Right);
}

TEST_CASE("Classify prefers a corner over the two edges that meet there")
{
    const CaptionMetrics metrics = Metrics();

    CHECK(Classify(metrics, kWidth, kHeight, 2.0f, 2.0f, false) == CaptionHit::TopLeft);
    CHECK(Classify(metrics, kWidth, kHeight, 798.0f, 2.0f, false) == CaptionHit::TopRight);
    CHECK(Classify(metrics, kWidth, kHeight, 2.0f, 598.0f, false) == CaptionHit::BottomLeft);
    CHECK(Classify(metrics, kWidth, kHeight, 798.0f, 598.0f, false) == CaptionHit::BottomRight);
}

TEST_CASE("The border outside the client area resizes like the edge it sits on")
{
    // The window keeps one pixel of frame on each edge for the system to draw its border
    // into, so the hit test is asked about points that are outside the client rectangle
    // and convert to negative — or past-the-end — client coordinates. They belong to the
    // edge they lie beyond, which is what the arithmetic already says; these cases are
    // here so that stays true, because it is now a reachable input and not a curiosity.
    const CaptionMetrics metrics = Metrics();

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, -1.0f, false) == CaptionHit::Top);
    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, kHeight, false) == CaptionHit::Bottom);
    CHECK(Classify(metrics, kWidth, kHeight, -1.0f, 300.0f, false) == CaptionHit::Left);
    CHECK(Classify(metrics, kWidth, kHeight, kWidth, 300.0f, false) == CaptionHit::Right);

    CHECK(Classify(metrics, kWidth, kHeight, -1.0f, -1.0f, false) == CaptionHit::TopLeft);
    CHECK(Classify(metrics, kWidth, kHeight, kWidth, -1.0f, false) == CaptionHit::TopRight);
    CHECK(Classify(metrics, kWidth, kHeight, -1.0f, kHeight, false) == CaptionHit::BottomLeft);
    CHECK(Classify(metrics, kWidth, kHeight, kWidth, kHeight, false) ==
          CaptionHit::BottomRight);
}

TEST_CASE("A maximized window has no resize edges")
{
    const CaptionMetrics metrics = Metrics();

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 2.0f, true) == CaptionHit::Caption);

    // Over the menus rather than the free span, so client area — but the point of the
    // check is that it is not TopLeft.
    CHECK(Classify(metrics, kWidth, kHeight, 2.0f, 2.0f, true) == CaptionHit::Client);
    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 598.0f, true) == CaptionHit::Client);
}

TEST_CASE("Only the span between the menus and the window buttons drags the window")
{
    const CaptionMetrics metrics = Metrics();

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 20.0f, false) == CaptionHit::Caption);

    // A menu or a window button owns the click at these two points, so they have to read
    // as client area — otherwise the strip would swallow every menu click.
    CHECK(Classify(metrics, kWidth, kHeight, 100.0f, 20.0f, false) == CaptionHit::Client);
    CHECK(Classify(metrics, kWidth, kHeight, 750.0f, 20.0f, false) == CaptionHit::Client);
}

TEST_CASE("The free span takes its first pixel and gives up its last")
{
    const CaptionMetrics metrics = Metrics();

    CHECK(Classify(metrics, kWidth, kHeight, kDragBegin, 20.0f, false) == CaptionHit::Caption);
    CHECK(Classify(metrics, kWidth, kHeight, kDragBegin - 1.0f, 20.0f, false) ==
          CaptionHit::Client);
    CHECK(Classify(metrics, kWidth, kHeight, kDragEnd - 1.0f, 20.0f, false) ==
          CaptionHit::Caption);
    CHECK(Classify(metrics, kWidth, kHeight, kDragEnd, 20.0f, false) == CaptionHit::Client);
}

TEST_CASE("A caption whose free span is empty drags nowhere")
{
    // What a window too narrow for its own buttons reports: the buttons have run past the
    // menus, so there is no free caption left anywhere in the row.
    CaptionMetrics metrics = Metrics();
    metrics.dragEnd = metrics.dragBegin;

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 20.0f, false) == CaptionHit::Client);

    metrics.dragEnd = -40.0f;
    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 20.0f, false) == CaptionHit::Client);
}

TEST_CASE("A suppressed drag leaves the whole caption to the application")
{
    CaptionMetrics metrics = Metrics();
    metrics.dragSuppressed = true;

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 20.0f, false) == CaptionHit::Client);

    // Only the caption is withheld: an open menu is no reason to stop resizing.
    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 2.0f, false) == CaptionHit::Top);
}

TEST_CASE("Everything below the caption is client area")
{
    CHECK(Classify(Metrics(), kWidth, kHeight, 400.0f, 33.0f, false) == CaptionHit::Client);
    CHECK(Classify(Metrics(), kWidth, kHeight, 400.0f, 300.0f, false) == CaptionHit::Client);
}

TEST_CASE("A caption shorter than the resize border still resizes from the top")
{
    CaptionMetrics metrics = Metrics();
    metrics.height = 4.0f;

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 2.0f, false) == CaptionHit::Top);
}
