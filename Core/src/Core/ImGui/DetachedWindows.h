#pragma once

#include "Core/Base/Color.h"

struct ImGuiPlatformIO;

namespace Core {

	void InstallDetachedWindowHooks(ImGuiPlatformIO& platformIo);

	void SetDetachedWindowBorderColor(const Color& color);

	void DrawDetachedWindowButtons();

	void ApplyPendingDetachedWindowActions();
}
