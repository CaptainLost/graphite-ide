#include "corepch.h"

#include "Core/ImGui/CaptionLayout.h"

namespace Core {

	namespace {
		constexpr float kBarTop = 0.0f;

		CaptionRect RectAcrossBar(float left, float width, float barHeight)
		{
			return CaptionRect{.X = left, .Y = kBarTop, .Width = width, .Height = barHeight};
		}
	}

	CaptionLayout LayOutCaption(float barWidth, float barHeight, float buttonWidth,
								float versionWidth, float versionGap, float contentLeft)
	{
		CaptionLayout layout;

		// WARNING: measured from the right edge, so a bar narrower than its own buttons runs off the left rather than pushing close off screen, and minimize is the first one to go.
		layout.Close = RectAcrossBar(barWidth - buttonWidth, buttonWidth, barHeight);
		layout.Maximize = RectAcrossBar(layout.Close.X - buttonWidth, buttonWidth, barHeight);
		layout.Minimize = RectAcrossBar(layout.Maximize.X - buttonWidth, buttonWidth, barHeight);

		const float versionLeft = layout.Minimize.X - versionGap - versionWidth;
		layout.Version = RectAcrossBar(versionLeft, versionWidth, barHeight);
		layout.VersionVisible = versionLeft >= contentLeft;

		return layout;
	}
}
