#include "corepch.h"

#include "Core/ImGui/Checkbox.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <optional>

namespace Core {

	namespace {

		constexpr float kOutlineWidth = 1.5f;
		constexpr float kCornerRadius = 4.0f;
		constexpr float kTickWidth = 2.0f;

		// How much of the accent a ticked box's inside takes on.
		constexpr float kTickedTintAlpha = 0.16f;

		// The tick's three points, as shares of the box's side from its top-left corner.
		constexpr ImVec2 kTickStart(0.24f, 0.52f);
		constexpr ImVec2 kTickBend(0.42f, 0.70f);
		constexpr ImVec2 kTickEnd(0.77f, 0.30f);

		constexpr bool kHideTextAfterDoubleHash = true;

		ImVec2 LabelSize(const char* label)
		{
			const char* wholeLabel = nullptr;
			return ImGui::CalcTextSize(label, wholeLabel, kHideTextAfterDoubleHash);
		}

		float WidthOf(const ImVec2& labelSize)
		{
			const float box = Scaled(kCheckboxSize);

			if (labelSize.x <= 0.0f)
			{
				return box;
			}

			return box + ImGui::GetStyle().ItemInnerSpacing.x + labelSize.x;
		}

		ImVec2 PointIn(const ImVec2& topLeft, float side, const ImVec2& share)
		{
			return ImVec2(topLeft.x + side * share.x, topLeft.y + side * share.y);
		}

		ImVec4 WithAlpha(const ImVec4& colour, float alpha)
		{
			return ImVec4(colour.x, colour.y, colour.z, colour.w * alpha);
		}

		bool DrawCheckbox(const char* label, bool* value, const std::optional<ImVec4>& outline)
		{
			const ImVec2 labelSize = LabelSize(label);
			const float side = Scaled(kCheckboxSize);
			const ImVec2 itemSize(WidthOf(labelSize), ImGui::GetFrameHeight());
			const ImVec2 itemTopLeft = ImGui::GetCursorScreenPos();

			const bool clicked = ImGui::InvisibleButton(label, itemSize, ImGuiButtonFlags_EnableNav);
			const ImGuiID itemId = ImGui::GetItemID();

			if (clicked)
			{
				*value = !*value;
				ImGui::MarkItemEdited(itemId);
			}

			const float ticked = Animation::SwitchAmount(itemId, *value);
			const float hover = Animation::HoverAmount(itemId, ImGui::IsItemHovered());

			const ImVec4 restingOutline = outline.value_or(ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
			const ImVec4 hoveredOutline =
				Animation::Blend(restingOutline, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), hover);
			const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
			const ImVec4 boxOutline = Animation::Blend(hoveredOutline, accent, ticked);

			const ImVec2 boxTopLeft(itemTopLeft.x, itemTopLeft.y + (itemSize.y - side) * 0.5f);
			const ImVec2 boxBottomRight(boxTopLeft.x + side, boxTopLeft.y + side);
			const float radius = Scaled(kCornerRadius);
			const float outlineWidth = Scaled(kOutlineWidth);

			// A stroke is centred on its rectangle; inset by half its width, it stays inside the box.
			const float inset = outlineWidth * 0.5f;

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(boxTopLeft, boxBottomRight,
									ImGui::GetColorU32(WithAlpha(accent, kTickedTintAlpha * ticked)), radius);
			drawList->AddRect(ImVec2(boxTopLeft.x + inset, boxTopLeft.y + inset),
							  ImVec2(boxBottomRight.x - inset, boxBottomRight.y - inset), ImGui::GetColorU32(boxOutline),
							  radius, ImDrawFlags_None, outlineWidth);

			if (ticked > 0.0f)
			{
				const std::array<ImVec2, 3> tick = {PointIn(boxTopLeft, side, kTickStart),
													PointIn(boxTopLeft, side, kTickBend),
													PointIn(boxTopLeft, side, kTickEnd)};
				drawList->AddPolyline(tick.data(), static_cast<int>(tick.size()),
									  ImGui::GetColorU32(WithAlpha(accent, ticked)), ImDrawFlags_None,
									  Scaled(kTickWidth));
			}

			if (labelSize.x > 0.0f)
			{
				const ImGuiStyle& style = ImGui::GetStyle();
				const ImVec2 labelTopLeft(boxBottomRight.x + style.ItemInnerSpacing.x,
										  itemTopLeft.y + style.FramePadding.y);
				ImGui::RenderText(labelTopLeft, label);
			}

			return clicked;
		}
	}

	bool Checkbox(const char* label, bool* value)
	{
		const std::optional<ImVec4> styleOutline;
		return DrawCheckbox(label, value, styleOutline);
	}

	bool Checkbox(const char* label, bool* value, const ImVec4& outline)
	{
		return DrawCheckbox(label, value, outline);
	}

	float CheckboxWidth(const char* label)
	{
		return WidthOf(LabelSize(label));
	}
}
