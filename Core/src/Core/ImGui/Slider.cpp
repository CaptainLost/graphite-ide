#include "corepch.h"

#include "Core/ImGui/Slider.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <format>
#include <string>

namespace Core {

	namespace {
		constexpr float kHalf = 0.5f;
		constexpr float kValueGap = 10.0f;
		constexpr float kHoveredKnobGrowShare = 0.12f;
		constexpr float kHoveredTrackTextShare = 0.12f;
		constexpr float kKnobRingThickness = 1.5f;

		// WARNING: the knob is white on every scheme for the same reason the toggle's is: the text colour is near black in the light one.
		// Unlike the toggle's it sits mostly on the card, which is white too in a light scheme, so its ring is what shows it there.
		constexpr ImVec4 kKnobColour(1.0f, 1.0f, 1.0f, 1.0f);

		std::string ValueText(int value, std::string_view unit)
		{
			return std::format("{} {}", value, unit);
		}

		float FractionOf(int value, int smallest, int largest)
		{
			return largest > smallest ? static_cast<float>(value - smallest) / static_cast<float>(largest - smallest) : 0.0f;
		}
	}

	bool SliderInt(const char* id, int* value, int smallest, int largest, std::string_view unit, float width)
	{
		// WARNING: the button below adds no item in a window that skips them, so the item id read after it would be the previous item's.
		if (ImGui::GetCurrentWindowRead()->SkipItems)
		{
			return false;
		}

		const float knobRadius = Scaled(kSliderKnobDiameter) * kHalf;
		const float widestValue = std::max(ImGui::CalcTextSize(ValueText(smallest, unit).c_str()).x, ImGui::CalcTextSize(ValueText(largest, unit).c_str()).x);
		const float valueSlot = widestValue + Scaled(kValueGap);

		const ImVec2 itemSize(width, ImGui::GetFrameHeight());
		const ImVec2 itemTopLeft = ImGui::GetCursorScreenPos();

		ImGui::InvisibleButton(id, itemSize, ImGuiButtonFlags_EnableNav);
		const ImGuiID itemId = ImGui::GetItemID();

		const float travelLeft = itemTopLeft.x + knobRadius;
		const float travelRight = itemTopLeft.x + width - valueSlot - knobRadius;
		const float travel = std::max(travelRight - travelLeft, 1.0f);

		bool changed = false;

		if (ImGui::IsItemActive())
		{
			const float fraction = std::clamp((ImGui::GetIO().MousePos.x - travelLeft) / travel, 0.0f, 1.0f);
			const int dragged = smallest + static_cast<int>(std::lround(fraction * static_cast<float>(largest - smallest)));

			if (dragged != *value)
			{
				*value = dragged;
				changed = true;
				ImGui::MarkItemEdited(itemId);
			}
		}

		const float hoverAmount = Animation::HoverAmount(itemId, ImGui::IsItemHovered() || ImGui::IsItemActive());
		const float fraction = FractionOf(*value, smallest, largest);

		const float trackHeight = Scaled(kSliderTrackHeight);
		const float centreY = itemTopLeft.y + itemSize.y * kHalf;
		const ImVec2 trackTopLeft(travelLeft - knobRadius, centreY - trackHeight * kHalf);
		const ImVec2 trackBottomRight(travelRight + knobRadius, centreY + trackHeight * kHalf);
		const float knobCentreX = travelLeft + travel * fraction;

		const ImVec4 emptyTrack = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive);
		// WARNING: Disabled, the slider is drawn grey rather than only fainter: the accent at reduced alpha still read as
		// a control that moves.
		const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
		const ImVec4 filledTrack = ImGui::GetStyleColorVec4(disabled ? ImGuiCol_TextDisabled : ImGuiCol_SliderGrab);
		const ImVec4 hoveredFilled = Animation::Blend(filledTrack, ImGui::GetStyleColorVec4(ImGuiCol_Text), kHoveredTrackTextShare);

		const ImU32 filledColour = ImGui::GetColorU32(Animation::Blend(filledTrack, hoveredFilled, hoverAmount));
		const ImVec2 knobCentre(knobCentreX, centreY);
		const float shownKnobRadius = knobRadius * (1.0f + kHoveredKnobGrowShare * hoverAmount);
		const float ringThickness = Scaled(kKnobRingThickness);

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(trackTopLeft, trackBottomRight, ImGui::GetColorU32(emptyTrack), trackHeight * kHalf);
		drawList->AddRectFilled(trackTopLeft, ImVec2(knobCentreX, trackBottomRight.y), filledColour, trackHeight * kHalf);
		drawList->AddCircleFilled(knobCentre, shownKnobRadius, ImGui::GetColorU32(kKnobColour));
		drawList->AddCircle(knobCentre, shownKnobRadius - ringThickness * kHalf, filledColour, 0, ringThickness);

		const std::string text = ValueText(*value, unit);
		const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
		const ImVec2 textPosition(itemTopLeft.x + width - textWidth, itemTopLeft.y + (itemSize.y - ImGui::GetTextLineHeight()) * kHalf);
		drawList->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), text.c_str());

		return changed;
	}
}
