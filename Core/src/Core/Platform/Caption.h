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
		float height = 0.0f;

		float resizeBorder = 8.0f;

		float dragBegin = 0.0f;
		float dragEnd = 0.0f;

		bool dragSuppressed = false;
	};

	CaptionHit Classify(const CaptionMetrics& metrics, float clientWidth,
						float clientHeight, float x, float y, bool maximized);
}