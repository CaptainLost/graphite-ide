#include "UI/Lists/ListRow.h"

#include "Core/ImGui/UiScale.h"

namespace Graphite {

	namespace {
		constexpr float kEndInset = 8.0f;
		constexpr float kHalf = 0.5f;
	}

	float ListRowPitch()
	{
		return ImGui::GetTextLineHeightWithSpacing();
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
	// whose work rect is as wide as its content, would only ever grow wider.
	bool ListRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags)
	{
		constexpr float kToTheWorkRectEdge = 0.0f;
		const ImVec2 rowStart = ImGui::GetCursorScreenPos();

		ImGui::SetCursorScreenPos(ImVec2(ListLeft(), rowStart.y));

		return ImGui::Selectable(id, selected, flags, ImVec2(kToTheWorkRectEdge, ImGui::GetTextLineHeight()));
	}

	ImVec2 ListTableCellPadding()
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		return ImVec2(style.CellPadding.x, style.ItemSpacing.y * kHalf);
	}
}
