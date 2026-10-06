#pragma once

#include <span>

namespace Core {

	// The rectangle is the one the window has when neither minimized nor maximized, kept while it is
	// maximized so it can return there. On Windows it is in workspace coordinates, as
	// GetWindowPlacement reports it.
	struct WindowPlacement
	{
		int X = 0;
		int Y = 0;
		int Width = 0;
		int Height = 0;
		bool Maximized = false;

		friend bool operator==(const WindowPlacement&, const WindowPlacement&) = default;
	};

	struct ScreenRect
	{
		int Left = 0;
		int Top = 0;
		int Right = 0;
		int Bottom = 0;

		friend bool operator==(const ScreenRect&, const ScreenRect&) = default;
	};

	WindowPlacement DefaultPlacement(int preferredWidth, int preferredHeight, const ScreenRect& workArea);

	WindowPlacement FitToMonitors(WindowPlacement placement, std::span<const ScreenRect> workAreas,
								  const ScreenRect& primaryWorkArea);
}
