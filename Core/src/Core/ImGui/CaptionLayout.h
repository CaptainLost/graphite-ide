#pragma once

namespace Core {

	struct CaptionRect
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	struct CaptionLayout
	{
		CaptionRect minimize;
		CaptionRect maximize;
		CaptionRect close;
		CaptionRect version;

		bool versionVisible = false;
	};

	CaptionLayout LayOutCaption(float barWidth, float barHeight,
								float buttonWidth, float versionWidth,
								float versionGap, float contentLeft);
}