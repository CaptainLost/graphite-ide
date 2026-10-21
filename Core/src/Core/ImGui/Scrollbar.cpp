#include "corepch.h"

#include "Core/ImGui/Scrollbar.h"

#include <imgui_internal.h>

namespace Core {

	namespace {
		constexpr float kHalf = 0.5f;
	}

	float ScrollbarGrabInset(float padding, ImVec2 barSize)
	{
		return IM_TRUNC(ImMin(padding, ImMin(barSize.x, barSize.y) * kHalf));
	}
}
