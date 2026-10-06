#include <doctest/doctest.h>

#include "Core/Platform/Caption.h"

using Core::CaptionHit;
using Core::CaptionMetrics;
using Core::Classify;
using Core::WindowState;

namespace {
	constexpr float kWidth = 800.0f;
	constexpr float kHeight = 600.0f;

	constexpr float kCaptionHeight = 32.0f;
	constexpr float kResizeBorder = 8.0f;

	constexpr float kDragBegin = 200.0f;
	constexpr float kDragEnd = 700.0f;

	constexpr float kMiddleX = 400.0f;
	constexpr float kMiddleY = 300.0f;

	constexpr float kInsideLeftBorder = 2.0f;
	constexpr float kInsideRightBorder = 798.0f;
	constexpr float kInsideTopBorder = 2.0f;
	constexpr float kInsideBottomBorder = 598.0f;

	constexpr float kFramePixelLeftOfClient = -1.0f;
	constexpr float kFramePixelRightOfClient = kWidth;
	constexpr float kFramePixelAboveClient = -1.0f;
	constexpr float kFramePixelBelowClient = kHeight;

	constexpr float kInsideCaption = 20.0f;
	constexpr float kJustBelowCaption = 33.0f;

	constexpr float kOverMenus = 100.0f;
	constexpr float kOverWindowButtons = 750.0f;

	constexpr float kOnePixel = 1.0f;
	constexpr float kButtonsBeginLeftOfWindow = -40.0f;
	constexpr float kCaptionShorterThanResizeBorder = 4.0f;

	CaptionMetrics CaptionBetweenMenusAndButtons()
	{
		return CaptionMetrics{.Height = kCaptionHeight, .ResizeBorder = kResizeBorder, .DragBegin = kDragBegin, .DragEnd = kDragEnd};
	}
}

TEST_CASE("Classify finds the resize edges of a restored window")
{
	const CaptionMetrics metrics = CaptionBetweenMenusAndButtons();

	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideTopBorder, WindowState::Restored) == CaptionHit::Top);
	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideBottomBorder, WindowState::Restored) == CaptionHit::Bottom);
	CHECK(Classify(metrics, kWidth, kHeight, kInsideLeftBorder, kMiddleY, WindowState::Restored) == CaptionHit::Left);
	CHECK(Classify(metrics, kWidth, kHeight, kInsideRightBorder, kMiddleY, WindowState::Restored) == CaptionHit::Right);
}

TEST_CASE("Classify prefers a corner over the two edges that meet there")
{
	const CaptionMetrics metrics = CaptionBetweenMenusAndButtons();

	CHECK(Classify(metrics, kWidth, kHeight, kInsideLeftBorder, kInsideTopBorder, WindowState::Restored) == CaptionHit::TopLeft);
	CHECK(Classify(metrics, kWidth, kHeight, kInsideRightBorder, kInsideTopBorder, WindowState::Restored) == CaptionHit::TopRight);
	CHECK(Classify(metrics, kWidth, kHeight, kInsideLeftBorder, kInsideBottomBorder, WindowState::Restored) == CaptionHit::BottomLeft);
	CHECK(Classify(metrics, kWidth, kHeight, kInsideRightBorder, kInsideBottomBorder, WindowState::Restored) == CaptionHit::BottomRight);
}

TEST_CASE("The frame pixel the system draws its border into outside each client edge resizes like that edge")
{
	const CaptionMetrics metrics = CaptionBetweenMenusAndButtons();

	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kFramePixelAboveClient, WindowState::Restored) == CaptionHit::Top);
	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kFramePixelBelowClient, WindowState::Restored) == CaptionHit::Bottom);
	CHECK(Classify(metrics, kWidth, kHeight, kFramePixelLeftOfClient, kMiddleY, WindowState::Restored) == CaptionHit::Left);
	CHECK(Classify(metrics, kWidth, kHeight, kFramePixelRightOfClient, kMiddleY, WindowState::Restored) == CaptionHit::Right);

	CHECK(Classify(metrics, kWidth, kHeight, kFramePixelLeftOfClient, kFramePixelAboveClient, WindowState::Restored) == CaptionHit::TopLeft);
	CHECK(Classify(metrics, kWidth, kHeight, kFramePixelRightOfClient, kFramePixelAboveClient, WindowState::Restored) == CaptionHit::TopRight);
	CHECK(Classify(metrics, kWidth, kHeight, kFramePixelLeftOfClient, kFramePixelBelowClient, WindowState::Restored) == CaptionHit::BottomLeft);
	CHECK(Classify(metrics, kWidth, kHeight, kFramePixelRightOfClient, kFramePixelBelowClient, WindowState::Restored) == CaptionHit::BottomRight);
}

TEST_CASE("A maximized window has no resize edges")
{
	const CaptionMetrics metrics = CaptionBetweenMenusAndButtons();

	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideTopBorder, WindowState::Maximized) == CaptionHit::Caption);

	const CaptionHit topLeftCornerOverTheMenus = Classify(metrics, kWidth, kHeight, kInsideLeftBorder, kInsideTopBorder, WindowState::Maximized);
	CHECK(topLeftCornerOverTheMenus == CaptionHit::Client);
	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideBottomBorder, WindowState::Maximized) == CaptionHit::Client);
}

TEST_CASE("Only the span between the menus and the window buttons drags the window so menu and button clicks reach the application")
{
	const CaptionMetrics metrics = CaptionBetweenMenusAndButtons();

	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideCaption, WindowState::Restored) == CaptionHit::Caption);

	CHECK(Classify(metrics, kWidth, kHeight, kOverMenus, kInsideCaption, WindowState::Restored) == CaptionHit::Client);
	CHECK(Classify(metrics, kWidth, kHeight, kOverWindowButtons, kInsideCaption, WindowState::Restored) == CaptionHit::Client);
}

TEST_CASE("The free span takes its first pixel and gives up its last")
{
	const CaptionMetrics metrics = CaptionBetweenMenusAndButtons();

	CHECK(Classify(metrics, kWidth, kHeight, kDragBegin, kInsideCaption, WindowState::Restored) == CaptionHit::Caption);
	CHECK(Classify(metrics, kWidth, kHeight, kDragBegin - kOnePixel, kInsideCaption, WindowState::Restored) == CaptionHit::Client);
	CHECK(Classify(metrics, kWidth, kHeight, kDragEnd - kOnePixel, kInsideCaption, WindowState::Restored) == CaptionHit::Caption);
	CHECK(Classify(metrics, kWidth, kHeight, kDragEnd, kInsideCaption, WindowState::Restored) == CaptionHit::Client);
}

TEST_CASE("A window too narrow for its buttons has an empty or inverted free span and drags nowhere")
{
	CaptionMetrics metrics = CaptionBetweenMenusAndButtons();
	metrics.DragEnd = metrics.DragBegin;

	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideCaption, WindowState::Restored) == CaptionHit::Client);

	metrics.DragEnd = kButtonsBeginLeftOfWindow;
	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideCaption, WindowState::Restored) == CaptionHit::Client);
}

TEST_CASE("A suppressed drag leaves the whole caption to the application but not the resize edges since an open menu is no reason to stop resizing")
{
	CaptionMetrics metrics = CaptionBetweenMenusAndButtons();
	metrics.DragSuppressed = true;

	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideCaption, WindowState::Restored) == CaptionHit::Client);
	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideTopBorder, WindowState::Restored) == CaptionHit::Top);
}

TEST_CASE("Everything below the caption is client area")
{
	CHECK(Classify(CaptionBetweenMenusAndButtons(), kWidth, kHeight, kMiddleX, kJustBelowCaption, WindowState::Restored) == CaptionHit::Client);
	CHECK(Classify(CaptionBetweenMenusAndButtons(), kWidth, kHeight, kMiddleX, kMiddleY, WindowState::Restored) == CaptionHit::Client);
}

TEST_CASE("A caption shorter than the resize border still resizes from the top")
{
	CaptionMetrics metrics = CaptionBetweenMenusAndButtons();
	metrics.Height = kCaptionShorterThanResizeBorder;

	CHECK(Classify(metrics, kWidth, kHeight, kMiddleX, kInsideTopBorder, WindowState::Restored) == CaptionHit::Top);
}
