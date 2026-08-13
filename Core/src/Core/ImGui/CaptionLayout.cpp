#include "corepch.h"

#include "Core/ImGui/CaptionLayout.h"

namespace Core {

	CaptionLayout LayOutCaption(float barWidth, float barHeight, float buttonWidth,
								float versionWidth, float versionGap, float contentLeft)
	{
		CaptionLayout layout;

		// Measured from the right edge, so a bar narrower than its own buttons runs off the
		// left rather than pushing close off screen, and minimize is the first one to go.
		layout.Close = CaptionRect{barWidth - buttonWidth, 0.0f, buttonWidth, barHeight};
		layout.Maximize = CaptionRect{layout.Close.X - buttonWidth, 0.0f, buttonWidth, barHeight};
		layout.Minimize = CaptionRect{layout.Maximize.X - buttonWidth, 0.0f, buttonWidth, barHeight};

		const float versionX = layout.Minimize.X - versionGap - versionWidth;
		layout.Version = CaptionRect{versionX, 0.0f, versionWidth, barHeight};
		layout.VersionVisible = versionX >= contentLeft;

		return layout;
	}
}