#include "corepch.h"

#include "Core/ImGui/CaptionLayout.h"

namespace Core {

	CaptionLayout LayOutCaption(float barWidth, float barHeight, float buttonWidth,
								float versionWidth, float versionGap, float contentLeft)
	{
		CaptionLayout layout;

		layout.Close = CaptionRect{barWidth - buttonWidth, 0.0f, buttonWidth, barHeight};
		layout.Maximize = CaptionRect{layout.Close.X - buttonWidth, 0.0f, buttonWidth, barHeight};
		layout.Minimize = CaptionRect{layout.Maximize.X - buttonWidth, 0.0f, buttonWidth, barHeight};

		// A bar narrower than its own three buttons is left to run off the left edge
		// rather than being shifted right: measuring from the right edge keeps close
		// where it always is, and makes minimize — the button whose loss costs least —
		// the first one to go. Shifting instead would have pushed close off screen.

		const float versionX = layout.Minimize.X - versionGap - versionWidth;
		layout.Version = CaptionRect{versionX, 0.0f, versionWidth, barHeight};
		layout.VersionVisible = versionX >= contentLeft;

		return layout;
	}
}