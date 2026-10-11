#include "corepch.h"

#include "Core/ImGui/Toggle.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Core {

	namespace {
		// How far the knob sits inside the track, as a share of the track's height.
		constexpr float kKnobInsetShare = 0.15f;

		// How much of the text colour a fully hovered track takes on.
		constexpr float kHoverTint = 0.12f;
	}

	bool Toggle(const char* id, bool* value)
	{
		const ImVec2 trackSize = Scaled(ImVec2(kToggleWidth, kToggleHeight));
		const ImVec2 itemSize(trackSize.x, ImGui::GetFrameHeight());
		const ImVec2 itemTopLeft = ImGui::GetCursorScreenPos();

		const bool clicked = ImGui::InvisibleButton(id, itemSize, ImGuiButtonFlags_EnableNav);
		const ImGuiID itemId = ImGui::GetItemID();

		if (clicked)
		{
			*value = !*value;
			ImGui::MarkItemEdited(itemId);
		}

		const float slide = Animation::SwitchAmount(itemId, *value);
		const float hover = Animation::HoverAmount(itemId, ImGui::IsItemHovered());

		const ImVec4 restingTrack =
			Animation::Blend(ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive), ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), slide);
		const ImVec4 hoveredTrack = Animation::Blend(restingTrack, ImGui::GetStyleColorVec4(ImGuiCol_Text), kHoverTint);
		const ImVec4 track = Animation::Blend(restingTrack, hoveredTrack, hover);

		const ImVec2 trackTopLeft(itemTopLeft.x, itemTopLeft.y + (itemSize.y - trackSize.y) * 0.5f);
		const ImVec2 trackBottomRight(trackTopLeft.x + trackSize.x, trackTopLeft.y + trackSize.y);
		const float trackRadius = trackSize.y * 0.5f;
		const float knobRadius = trackRadius - trackSize.y * kKnobInsetShare;
		const float knobTravel = trackSize.x - 2.0f * trackRadius;
		const ImVec2 knobCentre(trackTopLeft.x + trackRadius + knobTravel * slide, trackTopLeft.y + trackRadius);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(trackTopLeft, trackBottomRight, ImGui::GetColorU32(track), trackRadius);
		drawList->AddCircleFilled(knobCentre, knobRadius, ImGui::GetColorU32(ImGuiCol_Text));

		return clicked;
	}
}
