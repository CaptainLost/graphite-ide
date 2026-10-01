#include "UI/IconButton.h"

#include "Core/ImGui/Animation.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>

#include <cmath>

namespace Graphite {

	float IconButtonSize()
	{
		return std::floor(ImGui::GetFontSize() + Core::Scaled(4.0f));
	}

	// Drawn rather than submitted as an ImGui::Button, which is what put the glyphs off centre:
	// Button centres its label inside the frame LESS FramePadding, and on a square the width of
	// a row the horizontal padding leaves an inner box narrower than the glyph — so the text no
	// longer fits, and RenderTextClipped falls back to aligning it left.
	//
	// The same shape TitleBar::CaptionButton already uses for its window buttons.
	bool DrawIconButton(const char* id, const char* glyph, bool engaged, const char* tooltip)
	{
		const float size = IconButtonSize();

		// Centred in the line the frame sets the height of.
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
							 std::floor((ImGui::GetFrameHeight() - size) * 0.5f));

		const ImVec2 min = ImGui::GetCursorScreenPos();
		const ImVec2 max(min.x + size, min.y + size);

		const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
		const bool held = ImGui::IsItemActive();
		const bool hovered = ImGui::IsItemHovered();

		const float hover = Core::Animation::HoverAmount(ImGui::GetItemID(), hovered);

		ImDrawList* draw = ImGui::GetWindowDrawList();

		// No fill at rest: a row of boxed buttons reads as a form. An engaged toggle keeps one,
		// because "on" has to be visible without hovering it. Hovering fades in from whichever
		// of those two it rests at; a press answers at once.
		const ImVec4 hoveredFill = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
		const ImVec4 restingFill =
			engaged ? ImGui::GetStyleColorVec4(ImGuiCol_Button)
					: ImVec4(hoveredFill.x, hoveredFill.y, hoveredFill.z, 0.0f);
		const ImVec4 fill = held ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
								 : Core::Animation::Blend(restingFill, hoveredFill, hover);

		if (fill.w > 0.0f)
		{
			draw->AddRectFilled(min, max, ImGui::GetColorU32(fill), ImGui::GetStyle().FrameRounding);
		}

		// Floored, not rounded: CalcTextSize rounds up, so a glyph centred onto a half pixel
		// is resampled and reads blurrier than the text beside it.
		const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
		const ImVec2 at(std::floor(min.x + (size - glyphSize.x) * 0.5f),
						std::floor(min.y + (size - glyphSize.y) * 0.5f));

		// The accent is the second signal that a toggle is on: the darker fill alone is what
		// ImGui uses for a button being pressed right now, so on and under-the-mouse would
		// otherwise look the same.
		draw->AddText(at, ImGui::GetColorU32(engaged ? ImGuiCol_CheckMark : ImGuiCol_Text), glyph);

		// The icon says nothing to someone meeting it for the first time, so this is not
		// decoration — it is the only place the button explains itself.
		ImGui::SetItemTooltip("%s", tooltip);

		return pressed;
	}

	// Shorter than the line, so it reads as a divider rather than as a wall. Drawn rather than
	// submitted for the same reason the icons are: it has to be centred against the frame
	// height, which ImGui's own vertical separator takes from the layout instead.
	void DrawIconSeparator()
	{
		const float height = std::floor(ImGui::GetFontSize() * 0.9f);

		const ImVec2 at = ImGui::GetCursorScreenPos();
		const float top = at.y + std::floor((ImGui::GetFrameHeight() - height) * 0.5f);

		ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, top), ImVec2(at.x, top + height),
										   ImGui::GetColorU32(ImGuiCol_Separator), Core::Scaled(kIconSeparatorWidth));

		// Claims the line it drew on, so the spacing either side of it is the ordinary item
		// spacing and nothing has to know a line went past.
		ImGui::Dummy(ImVec2(Core::Scaled(kIconSeparatorWidth), ImGui::GetFrameHeight()));
	}
}
