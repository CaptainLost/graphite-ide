#include "corepch.h"

#include "Core/ImGui/Toggle.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Core {

	namespace {
		constexpr float kKnobInsetShare = 0.15f;
		constexpr float kHoveredTrackTextShare = 0.12f;
		constexpr float kHalf = 0.5f;
		constexpr float kTrackEndCount = 2.0f;

		// WARNING: the knob is white on every scheme, because the text colour is near black in the light one and the knob would read as a hole in the track.
		constexpr ImVec4 kKnobColour(1.0f, 1.0f, 1.0f, 1.0f);
	}

	bool Toggle(const char* id, bool* isOn)
	{
		// WARNING: the button below adds no item in a window that skips them, so the item id read after
		// it would be the previous item's, and the animations would write over its stored state.
		if (ImGui::GetCurrentWindowRead()->SkipItems)
		{
			return false;
		}

		const ImVec2 trackSize = Scaled(ImVec2(kToggleWidth, kToggleHeight));
		const ImVec2 itemSize(trackSize.x, ImGui::GetFrameHeight());
		const ImVec2 itemTopLeft = ImGui::GetCursorScreenPos();

		const bool clicked = ImGui::InvisibleButton(id, itemSize, ImGuiButtonFlags_EnableNav);
		const ImGuiID itemId = ImGui::GetItemID();

		if (clicked)
		{
			*isOn = !*isOn;
			ImGui::MarkItemEdited(itemId);
		}

		const float knobSlideAmount = Animation::SwitchAmount(itemId, *isOn);
		const float hoverAmount = Animation::HoverAmount(itemId, ImGui::IsItemHovered());

		const ImVec4 restingTrack =
			Animation::Blend(ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive), ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), knobSlideAmount);
		const ImVec4 hoveredTrack = Animation::Blend(restingTrack, ImGui::GetStyleColorVec4(ImGuiCol_Text), kHoveredTrackTextShare);
		const ImVec4 track = Animation::Blend(restingTrack, hoveredTrack, hoverAmount);

		const ImVec2 trackTopLeft(itemTopLeft.x, itemTopLeft.y + (itemSize.y - trackSize.y) * kHalf);
		const ImVec2 trackBottomRight(trackTopLeft.x + trackSize.x, trackTopLeft.y + trackSize.y);
		const float trackRadius = trackSize.y * kHalf;
		const float knobRadius = trackRadius - trackSize.y * kKnobInsetShare;
		const float knobTravel = trackSize.x - kTrackEndCount * trackRadius;
		const ImVec2 knobCentre(trackTopLeft.x + trackRadius + knobTravel * knobSlideAmount, trackTopLeft.y + trackRadius);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(trackTopLeft, trackBottomRight, ImGui::GetColorU32(track), trackRadius);
		drawList->AddCircleFilled(knobCentre, knobRadius, ImGui::GetColorU32(kKnobColour));

		return clicked;
	}
}
