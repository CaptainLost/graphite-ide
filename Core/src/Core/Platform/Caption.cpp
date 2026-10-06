#include "corepch.h"

#include "Core/Platform/Caption.h"

#include "Core/Base/Assert.h"

namespace Core {

	namespace {
		CaptionHit ResizeEdgeOrCorner(bool onTopEdge, bool onBottomEdge, bool onLeftEdge, bool onRightEdge)
		{
			if (onTopEdge && onLeftEdge)
			{
				return CaptionHit::TopLeft;
			}
			if (onTopEdge && onRightEdge)
			{
				return CaptionHit::TopRight;
			}
			if (onBottomEdge && onLeftEdge)
			{
				return CaptionHit::BottomLeft;
			}
			if (onBottomEdge && onRightEdge)
			{
				return CaptionHit::BottomRight;
			}
			if (onTopEdge)
			{
				return CaptionHit::Top;
			}
			if (onBottomEdge)
			{
				return CaptionHit::Bottom;
			}
			if (onLeftEdge)
			{
				return CaptionHit::Left;
			}

			CORE_ASSERT(onRightEdge, "a point on the resize border is on at least one edge");
			return CaptionHit::Right;
		}
	}

	CaptionHit Classify(const CaptionMetrics& metrics, float clientWidth, float clientHeight,
						float x, float y, WindowState windowState)
	{
		if (windowState == WindowState::Restored)
		{
			const bool onTopEdge = y < metrics.ResizeBorder;
			const bool onBottomEdge = y >= clientHeight - metrics.ResizeBorder;
			const bool onLeftEdge = x < metrics.ResizeBorder;
			const bool onRightEdge = x >= clientWidth - metrics.ResizeBorder;
			const bool onAnyEdge = onTopEdge || onBottomEdge || onLeftEdge || onRightEdge;

			if (onAnyEdge)
			{
				return ResizeEdgeOrCorner(onTopEdge, onBottomEdge, onLeftEdge, onRightEdge);
			}
		}

		const bool insideCaptionRow = y < metrics.Height;
		const bool overFreeSpan = x >= metrics.DragBegin && x < metrics.DragEnd;
		const bool dragsWindow = insideCaptionRow && !metrics.DragSuppressed && overFreeSpan;

		if (dragsWindow)
		{
			return CaptionHit::Caption;
		}

		return CaptionHit::Client;
	}
}
