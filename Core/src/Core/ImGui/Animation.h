#pragma once

#include <imgui.h>

namespace Core {

	class Animation final
	{
	public:
		Animation() = delete;

		static constexpr float kHoverInSeconds = 0.2f;
		static constexpr float kHoverOutSeconds = 0.35f;
		static constexpr float kLongestStepSeconds = 0.1f;

		static void BeginFrame();

		static float HoverAmount(ImGuiID id, bool hovered);

		static constexpr float kSwitchSeconds = 0.12f;

		static float SwitchAmount(ImGuiID id, bool on);

		static ImVec4 Blend(const ImVec4& from, const ImVec4& to, float amount);
	};
}
