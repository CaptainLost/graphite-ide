#pragma once

namespace Core {

	enum class CaptionHit
	{
		Client,
		Caption,
		Left,
		Right,
		Top,
		Bottom,
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight
	};

	struct CaptionMetrics
	{
		float Height = 0.0f;

		float ResizeBorder = 8.0f;

		float DragBegin = 0.0f;
		float DragEnd = 0.0f;

		bool DragSuppressed = false;
	};

	CaptionHit Classify(const CaptionMetrics& metrics, float clientWidth,
						float clientHeight, float x, float y, bool maximized);
}