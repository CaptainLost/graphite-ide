#include "corepch.h"

#include "Core/Platform/Caption.h"

namespace Core {

	namespace {
		CaptionHit Corner(bool top, bool bottom, bool left, bool right)
		{
			if (top && left)
			{
				return CaptionHit::TopLeft;
			}
			if (top && right)
			{
				return CaptionHit::TopRight;
			}
			if (bottom && left)
			{
				return CaptionHit::BottomLeft;
			}
			if (bottom && right)
			{
				return CaptionHit::BottomRight;
			}
			if (top)
			{
				return CaptionHit::Top;
			}
			if (bottom)
			{
				return CaptionHit::Bottom;
			}
			if (left)
			{
				return CaptionHit::Left;
			}

			return CaptionHit::Right;
		}
	}

	CaptionHit Classify(const CaptionMetrics& metrics, float clientWidth, float clientHeight,
						float x, float y, bool maximized)
	{
		if (!maximized)
		{
			const bool top = y < metrics.ResizeBorder;
			const bool bottom = y >= clientHeight - metrics.ResizeBorder;
			const bool left = x < metrics.ResizeBorder;
			const bool right = x >= clientWidth - metrics.ResizeBorder;

			if (top || bottom || left || right)
			{
				return Corner(top, bottom, left, right);
			}
		}

		if (y < metrics.Height && !metrics.DragSuppressed && x >= metrics.DragBegin &&
			x < metrics.DragEnd)
		{
			return CaptionHit::Caption;
		}

		return CaptionHit::Client;
	}
}