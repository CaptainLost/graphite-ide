#include "UI/Lists/ListRow.h"

#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"

#include <array>
#include <optional>

namespace Graphite {

	namespace {
		constexpr float kListRowSpacing = 9.0f;
		constexpr float kHighlightInset = 6.0f;
		constexpr float kHighlightRounding = 5.0f;
		constexpr float kEndInset = 8.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kSelectedAlpha = 0.20f;
		constexpr float kPressedAlpha = 0.30f;
		constexpr float kHoverLiftTowardTheText = 0.06f;
		constexpr ImVec4 kClear(0.0f, 0.0f, 0.0f, 0.0f);

		constexpr std::array kColoursSelectableDraws{ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive,
													 ImGuiCol_NavCursor};

		// WARNING: Selectable fills its whole box square, and the box is as wide as the list so that a click anywhere
		// across it lands on the row. Its fill and its keyboard mark are cleared for the call and drawn again inset;
		// cleared, they add nothing to the draw list.
		void PushClearedSelectableColours()
		{
			for (const ImGuiCol colour : kColoursSelectableDraws)
			{
				ImGui::PushStyleColor(colour, kClear);
			}
		}

		void PopClearedSelectableColours()
		{
			ImGui::PopStyleColor(static_cast<int>(kColoursSelectableDraws.size()));
		}

		// WARNING: The order is Selectable's own: a row under the pointer shows the hover whether it is selected or
		// not, and one held down under the pointer shows the press.
		std::optional<ImVec4> HighlightOf(bool selected, ImGuiSelectableFlags flags)
		{
			const ListRowColours colours = CurrentListRowColours();
			const bool highlighted = ImGui::IsItemHovered() || (flags & ImGuiSelectableFlags_Highlight) != 0;

			if (highlighted && ImGui::IsItemActive())
			{
				return colours.Pressed;
			}

			if (highlighted)
			{
				return colours.Hovered;
			}

			if (selected)
			{
				return colours.Selected;
			}

			return std::nullopt;
		}

		// WARNING: Call this while the row's Selectable is the last item. A Selectable disabled by its flag draws its
		// fill disabled, so the inset fill is drawn disabled too.
		void DrawHighlight(bool selected, ImGuiSelectableFlags flags)
		{
			const float inset = Core::Scaled(kHighlightInset);
			const float rounding = Core::Scaled(kHighlightRounding);
			const ImRect highlight(ListLeft() + inset, ImGui::GetItemRectMin().y, ListRight() - inset,
								   ImGui::GetItemRectMax().y);

			const std::optional<ImVec4> colour = HighlightOf(selected, flags);

			if (colour.has_value())
			{
				ImGui::BeginDisabled((flags & ImGuiSelectableFlags_Disabled) != 0);
				ImGui::GetWindowDrawList()->AddRectFilled(highlight.Min, highlight.Max, ImGui::GetColorU32(*colour),
														  rounding);
				ImGui::EndDisabled();
			}

			ImGui::RenderNavCursor(highlight, ImGui::GetItemID(), ImGuiNavRenderCursorFlags_Compact, rounding);
		}

		// WARNING: Callers lay a row's pieces over it after the Selectable, from the row's start or with SameLine, and
		// ImGui ends each line its pieces stand on at the tallest of them plus the item spacing. A line of text alone
		// would put the next row a text line and the item spacing below, short of the pitch, so the row's line is
		// held as tall as the pitch less the item spacing for whatever is laid on it. A row nothing is laid over
		// leaves that height standing for the next item: the next row clears it before its own Selectable, and any
		// other item takes a line at least that tall.
		void HoldTheRowLine(ImGuiWindow& window)
		{
			const float rowLine = ListRowLineHeight();
			window.DC.PrevLineSize.y = rowLine;
			window.DC.CurrLineSize.y = rowLine;
		}
	}

	ListRowColours CurrentListRowColours()
	{
		const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
		const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);

		return ListRowColours{.Selected = Core::WithAlphaReplaced(accent, kSelectedAlpha),
							  .Hovered = ImLerp(background, text, kHoverLiftTowardTheText),
							  .Pressed = Core::WithAlphaReplaced(accent, kPressedAlpha)};
	}

	float ListRowSpacing()
	{
		return Core::Scaled(kListRowSpacing);
	}

	float ListRowPitch()
	{
		return ImGui::GetTextLineHeight() + ListRowSpacing();
	}

	float ListRowLineHeight()
	{
		return ListRowPitch() - ImGui::GetStyle().ItemSpacing.y;
	}

	float ListRowEndInset()
	{
		return Core::Scaled(kEndInset);
	}

	// WARNING: InnerRect is the window's visible inside, clear of its scrollbars. The work rect and the content
	// region move with a sideways scroll, and a row's end placed on them would slide out of view.
	float ListRowEnd()
	{
		return ListRight() - ListRowEndInset();
	}

	float ListRowEndPosX()
	{
		return ListRowEnd() - ImGui::GetWindowPos().x + ImGui::GetScrollX();
	}

	float ListLeft()
	{
		return ImGui::GetCurrentWindowRead()->InnerRect.Min.x;
	}

	float ListRight()
	{
		return ImGui::GetCurrentWindowRead()->InnerRect.Max.x;
	}

	// WARNING: No width is given. ImGui then fills the row from the cursor to the work rect's edge, but lays out
	// only the id's empty label; a width laid out would widen the list's content, and a list that scrolls sideways,
	// whose work rect is as wide as its content, would only ever grow wider. The Selectable is a text line high
	// under the row spacing, which pads its box by half the spacing above the text and the rest below, so the text
	// laid at the row's start sits in the middle of the highlight.
	bool ListRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags)
	{
		ImGuiWindow* const window = ImGui::GetCurrentWindow();

		if (window->SkipItems)
		{
			return false;
		}

		constexpr float kToTheWorkRectEdge = 0.0f;
		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		const float itemSpacingX = ImGui::GetStyle().ItemSpacing.x;

		ImGui::SetCursorScreenPos(ImVec2(ListLeft(), rowStart.y));
		window->DC.CurrLineSize.y = 0.0f;

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacingX, ListRowSpacing()));
		PushClearedSelectableColours();
		const bool pressed =
			ImGui::Selectable(id, selected, flags, ImVec2(kToTheWorkRectEdge, ImGui::GetTextLineHeight()));
		PopClearedSelectableColours();
		ImGui::PopStyleVar();

		HoldTheRowLine(*window);

		if (ImGui::IsItemVisible())
		{
			DrawHighlight(selected, flags);
		}

		return pressed;
	}

	ImVec2 ListTableCellPadding()
	{
		return ImVec2(ImGui::GetStyle().CellPadding.x, ListRowSpacing() * kHalf);
	}
}
