#include "UI/TickBox.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <array>
#include <cmath>
#include <optional>

namespace Graphite {

	namespace {

		constexpr float kOutlineWidth = 1.5f;
		constexpr float kCornerRadius = 4.0f;
		constexpr float kTickWidth = 2.0f;

		// How much of the accent a ticked box's inside takes on.
		constexpr float kTickedTintAlpha = 0.16f;

		// An unticked outline is the disabled-text colour seen through: an opaque surface colour would
		// match whatever is filled with that same colour beneath it, as a hovered row is, and vanish.
		constexpr float kRestingOutlineAlpha = 0.4f;

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
			const float box = Core::Scaled(kTickBoxSize);

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

		bool DrawBox(const char* label, bool* value, const std::optional<ImVec4>& outline)
		{
			// The button below adds no item in a window that skips them, and the item id read after it
			// would then be the previous item's, whose stored state the animations would write over.
			if (ImGui::GetCurrentWindowRead()->SkipItems)
			{
				return false;
			}

			const ImVec2 labelSize = LabelSize(label);
			const float side = Core::Scaled(kTickBoxSize);
			const ImVec2 itemSize(WidthOf(labelSize), ImGui::GetFrameHeight());
			const ImVec2 itemTopLeft = ImGui::GetCursorScreenPos();

			// An invisible button leaves no text baseline behind it, so text put after the box with
			// SameLine would sit at the top of the line instead of where it sits after a frame.
			ImGui::AlignTextToFramePadding();
			const bool clicked = ImGui::InvisibleButton(label, itemSize, ImGuiButtonFlags_EnableNav);
			const ImGuiID itemId = ImGui::GetItemID();

			if (clicked)
			{
				*value = !*value;
				ImGui::MarkItemEdited(itemId);
			}

			const float ticked = Core::Animation::SwitchAmount(itemId, *value);
			const float hover = Core::Animation::HoverAmount(itemId, ImGui::IsItemHovered());

			const ImVec4 restingOutline =
				outline.value_or(WithAlpha(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), kRestingOutlineAlpha));
			const ImVec4 hoveredOutline =
				Core::Animation::Blend(restingOutline, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), hover);
			const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
			const ImVec4 boxOutline = Core::Animation::Blend(hoveredOutline, accent, ticked);

			// On a whole pixel: centred on a half one, the thin outline smears across two rows and fades.
			const ImVec2 boxTopLeft(itemTopLeft.x, itemTopLeft.y + std::floor((itemSize.y - side) * 0.5f));
			const ImVec2 boxBottomRight(boxTopLeft.x + side, boxTopLeft.y + side);
			const float radius = Core::Scaled(kCornerRadius);
			const float outlineWidth = Core::Scaled(kOutlineWidth);

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
									  Core::Scaled(kTickWidth));
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

	bool DrawTickBox(const char* label, bool* value)
	{
		const std::optional<ImVec4> styleOutline;
		return DrawBox(label, value, styleOutline);
	}

	bool DrawTickBox(const char* label, bool* value, const ImVec4& outline)
	{
		return DrawBox(label, value, outline);
	}

	float TickBoxWidth(const char* label)
	{
		return WidthOf(LabelSize(label));
	}
}
