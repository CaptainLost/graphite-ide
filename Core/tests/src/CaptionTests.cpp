#include <doctest/doctest.h>

#include "Core/Platform/Caption.h"

using core::CaptionHit;
using core::CaptionMetrics;
using core::Classify;

namespace
{
    constexpr float kWidth = 800.0f;
    constexpr float kHeight = 600.0f;

    CaptionMetrics Metrics(bool overDragArea)
    {
        CaptionMetrics metrics;
        metrics.height = 32.0f;
        metrics.resizeBorder = 8.0f;
        metrics.cursorOverDragArea = overDragArea;
        return metrics;
    }
}

TEST_CASE("Classify finds the resize edges of a restored window")
{
    const CaptionMetrics metrics = Metrics(true);

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 2.0f, false) == CaptionHit::Top);
    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 598.0f, false) == CaptionHit::Bottom);
    CHECK(Classify(metrics, kWidth, kHeight, 2.0f, 300.0f, false) == CaptionHit::Left);
    CHECK(Classify(metrics, kWidth, kHeight, 798.0f, 300.0f, false) == CaptionHit::Right);
}

TEST_CASE("Classify prefers a corner over the two edges that meet there")
{
    const CaptionMetrics metrics = Metrics(true);

    CHECK(Classify(metrics, kWidth, kHeight, 2.0f, 2.0f, false) == CaptionHit::TopLeft);
    CHECK(Classify(metrics, kWidth, kHeight, 798.0f, 2.0f, false) == CaptionHit::TopRight);
    CHECK(Classify(metrics, kWidth, kHeight, 2.0f, 598.0f, false) == CaptionHit::BottomLeft);
    CHECK(Classify(metrics, kWidth, kHeight, 798.0f, 598.0f, false) == CaptionHit::BottomRight);
}

TEST_CASE("A maximized window has no resize edges")
{
    const CaptionMetrics metrics = Metrics(true);

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 2.0f, true) == CaptionHit::Caption);
    CHECK(Classify(metrics, kWidth, kHeight, 2.0f, 2.0f, true) == CaptionHit::Caption);
    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 598.0f, true) == CaptionHit::Client);
}

TEST_CASE("The strip below the resize border drags the window only where no widget sits")
{
    CHECK(Classify(Metrics(true), kWidth, kHeight, 400.0f, 20.0f, false) == CaptionHit::Caption);

    // A menu or a window button under the cursor takes the click, so the same point
    // has to read as client area — otherwise the strip would swallow every menu click.
    CHECK(Classify(Metrics(false), kWidth, kHeight, 400.0f, 20.0f, false) == CaptionHit::Client);
}

TEST_CASE("Everything below the caption is client area")
{
    CHECK(Classify(Metrics(true), kWidth, kHeight, 400.0f, 33.0f, false) == CaptionHit::Client);
    CHECK(Classify(Metrics(true), kWidth, kHeight, 400.0f, 300.0f, false) == CaptionHit::Client);
}

TEST_CASE("A caption shorter than the resize border still resizes from the top")
{
    CaptionMetrics metrics = Metrics(true);
    metrics.height = 4.0f;

    CHECK(Classify(metrics, kWidth, kHeight, 400.0f, 2.0f, false) == CaptionHit::Top);
}
