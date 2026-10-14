#pragma once

namespace Core {

	struct CaptionRect
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;
	};

	struct CaptionLayout
	{
		CaptionRect Minimize;
		CaptionRect Maximize;
		CaptionRect Close;
		CaptionRect Version;

		bool VersionVisible = false;
	};

	CaptionLayout LayOutCaption(float barWidth, float barHeight,
								float buttonWidth, float versionWidth,
								float versionGap, float contentLeft);
}