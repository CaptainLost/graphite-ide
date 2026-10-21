#pragma once

#include <imgui.h>

namespace Core {

	// WARNING: A scrollbar drawn by hand insets its grab by this on every side, as ImGui's own do, or the grab comes
	// out the whole bar wide.
	float ScrollbarGrabInset(float padding, ImVec2 barSize);
}
