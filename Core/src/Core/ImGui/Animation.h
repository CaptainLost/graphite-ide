#pragma once

#include <imgui.h>

namespace Core {

	// A type of static members rather than a namespace of its own: the house style keeps one
	// namespace per project.
	class Animation final
	{
	public:
		Animation() = delete;

		// How long a hover takes to arrive and to leave. Leaving is the slower of the two: the
		// pointer has already moved on, so the fade only has to not snap, while arriving is
		// feedback the user is waiting for. Both are eased out, which spends most of the change
		// in the first third - shorter than this and the fade is over before it can be seen.
		static constexpr float kHoverInSeconds = 0.2f;
		static constexpr float kHoverOutSeconds = 0.35f;

		// Advances every animation by this frame's step. Called once per frame, after
		// ImGui::NewFrame, before anything asks for a value.
		static void BeginFrame();

		// How far an item is into its hover, from 0 at rest to 1 fully hovered, eased both ways.
		// The id is what keeps one item's fade apart from another's, so pass the item's own id -
		// ImGui::GetItemID() straight after the widget is the usual source. Asks the frame loop
		// for the next frame for as long as the fade is under way.
		static float HoverAmount(ImGuiID id, bool hovered);

		// A colour part of the way from one to the other, alpha included.
		static ImVec4 Blend(const ImVec4& from, const ImVec4& to, float amount);
	};
}
