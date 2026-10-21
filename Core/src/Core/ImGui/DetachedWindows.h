#pragma once

#include "Core/Base/Color.h"

struct ImGuiPlatformIO;

namespace Core {

	void InstallDetachedWindowHooks(ImGuiPlatformIO& platformIo);

	void SetDetachedWindowBorderColor(const Color& color);

	void DrawDetachedWindowButtons();

	void ApplyPendingDetachedWindowActions();

	// WARNING: A detached window ImGui drags onto a monitor at another scale is resized and moved by the system
	// inside that drag's own move, and UpdatePlatformWindows clears the request the resize raised in the same pass.
	// ImGui would then go on drawing the window at its old size, leaving the rest of it black. Run right after
	// UpdatePlatformWindows; it asks again for every window the system holds a pixel or more away from ImGui's
	// rect, and answers whether it asked for any, so that a frame follows to take them.
	bool AskDetachedWindowsForTheirSystemRects(ImGuiPlatformIO& platformIo);
}
