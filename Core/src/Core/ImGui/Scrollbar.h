#pragma once

#include <imgui.h>

namespace Core {

	// How far ImGui's own scrollbars keep their grab inside the bar on every side: the padding, never more than
	// half the bar's shorter side, cut to a whole pixel. A bar drawn by hand insets its grab by the same, or its
	// grab comes out the whole bar wide.
	float ScrollbarGrabInset(float padding, ImVec2 barSize);
}
