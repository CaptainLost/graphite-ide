#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <optional>

namespace Graphite {

	struct ListRowColours
	{
		ImVec4 Selected;
		ImVec4 Hovered;
		ImVec4 Pressed;
	};

	ListRowColours CurrentListRowColours();

	std::optional<ImVec4> ListRowHighlight(bool selected, bool hovered, bool held);

	ImU32 ListRowNameColour(bool selected);

	float ListRowSpacing();

	float ListRowPitch();

	float ListRowLineHeight();

	float ListRowTextTop(float rowTop);

	float ListRowEndInset();

	float ListRowEnd();

	float ListRowEndPosX();

	float ListLeft();
	float ListRight();

	bool ListRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None);

	// WARNING: ImGui ends a line at its tallest piece plus the item spacing, so a row's text, centred and laid on the
	// row's line, ends the row below its pitch while the item spacing is more than half the row spacing. A list body
	// caps the spacing at that half for whatever is laid out inside it; outside one, ListRowTextTop holds the text
	// higher to keep the pitch. It is made inside the list's window and must end before that window does.
	class ListBodyScope final
	{
	public:
		ListBodyScope();
		~ListBodyScope();

		ListBodyScope(const ListBodyScope&) = delete;
		ListBodyScope& operator=(const ListBodyScope&) = delete;
		ListBodyScope(ListBodyScope&&) = delete;
		ListBodyScope& operator=(ListBodyScope&&) = delete;
	};

	// WARNING: A popup is laid out with the style in force where it is drawn, so a menu opened from a row would take
	// the list body's capped spacing. Made before the popup is begun and ended after it, this gives the popup the
	// spacing outside the list body.
	class ListPopupScope final
	{
	public:
		ListPopupScope();
		~ListPopupScope();

		ListPopupScope(const ListPopupScope&) = delete;
		ListPopupScope& operator=(const ListPopupScope&) = delete;
		ListPopupScope(ListPopupScope&&) = delete;
		ListPopupScope& operator=(ListPopupScope&&) = delete;

	private:
		bool m_Pushed = false;
	};

	ImVec2 ListTableCellPadding();

	void PadListTableEnds();

	bool TableRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None);

	void DrawTableRowHighlight(bool selected, bool hovered);
}
