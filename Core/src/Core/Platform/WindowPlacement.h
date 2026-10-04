#pragma once

#include <span>

namespace Core {

	// A top-level window's place on the desktop, in the terms the platform restores it by: the
	// rectangle it has when neither minimized nor maximized, and whether it is maximized over it.
	// On Windows the rectangle is in workspace coordinates, as GetWindowPlacement reports it.
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

	// Where a window with no saved placement opens: `width` by `height`, shrunk to no more than
	// 85% of `workArea` in either direction so the window and its borders clear the taskbar and
	// the edges, and centred in it.
	WindowPlacement DefaultPlacement(int width, int height, const ScreenRect& workArea);

	// Keeps a placement that enough of the window shows on some monitor to be grabbed, and
	// otherwise centres it on the primary work area, shrunk to fit if it has to be. A window
	// restored onto a monitor that has since been unplugged would open where nobody can see it.
	WindowPlacement FitToMonitors(WindowPlacement placement, std::span<const ScreenRect> workAreas,
								  const ScreenRect& primary);
}
