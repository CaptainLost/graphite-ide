#include "UI/IconButton.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>

#include <cmath>

namespace Graphite {

	namespace {
		constexpr float kButtonInset = 4.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kSeparatorHeightPerFontSize = 0.9f;
		constexpr float kInvisibleAlpha = 0.0f;
	}

	float IconButtonSize()
	{
		return std::floor(ImGui::GetFontSize() + Core::Scaled(kButtonInset));
	}

	// WARNING: drawn rather than submitted as an ImGui::Button, which put the glyphs off centre: on
	// a square the width of a row, the frame padding leaves an inner box narrower than the glyph,
	// the text no longer fits, and RenderTextClipped falls back to aligning it left.
	bool DrawIconButton(const char* id, const char* glyph, bool engaged, const char* tooltip)
	{
		const float size = IconButtonSize();

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
							 std::floor((ImGui::GetFrameHeight() - size) * kHalf));

		const ImVec2 buttonMin = ImGui::GetCursorScreenPos();
		const ImVec2 buttonMax(buttonMin.x + size, buttonMin.y + size);

		const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
		const bool held = ImGui::IsItemActive();
		const bool hovered = ImGui::IsItemHovered();

		const float hoverAmount = Core::Animation::HoverAmount(ImGui::GetItemID(), hovered);

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const ImVec4 hoveredFill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
		const ImVec4 restingFill = engaged ? ImGui::GetStyleColorVec4(ImGuiCol_Button)
										   : Core::WithAlphaReplaced(hoveredFill, kInvisibleAlpha);
		const ImVec4 fill = held ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
								 : Core::Animation::Blend(restingFill, hoveredFill, hoverAmount);

		if (fill.w > 0.0f)
		{
			drawList->AddRectFilled(buttonMin, buttonMax, ImGui::GetColorU32(fill), ImGui::GetStyle().FrameRounding);
		}

		// WARNING: floored, not rounded: CalcTextSize rounds up, and a glyph centred onto a half pixel
		// is resampled and reads blurrier than the text beside it.
		const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
		const ImVec2 glyphPosition(std::floor(buttonMin.x + (size - glyphSize.x) * kHalf),
								   std::floor(buttonMin.y + (size - glyphSize.y) * kHalf));

		drawList->AddText(glyphPosition, ImGui::GetColorU32(engaged ? ImGuiCol_CheckMark : ImGuiCol_Text), glyph);

		ImGui::SetItemTooltip("%s", tooltip);

		return pressed;
	}

	void DrawIconSeparator()
	{
		const float height = std::floor(ImGui::GetFontSize() * kSeparatorHeightPerFontSize);

		const ImVec2 separatorStart = ImGui::GetCursorScreenPos();
		const float separatorTop = separatorStart.y + std::floor((ImGui::GetFrameHeight() - height) * kHalf);

		ImGui::GetWindowDrawList()->AddLine(ImVec2(separatorStart.x, separatorTop),
											ImVec2(separatorStart.x, separatorTop + height),
											ImGui::GetColorU32(ImGuiCol_Separator), Core::Scaled(kIconSeparatorWidth));

		ImGui::Dummy(ImVec2(Core::Scaled(kIconSeparatorWidth), ImGui::GetFrameHeight()));
	}
}
