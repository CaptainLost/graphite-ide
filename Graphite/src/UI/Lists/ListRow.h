#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace Graphite {

	// What every list row takes on the screen: a line of text and the item spacing under it. A row's highlight fills
	// all of it, so the highlights of rows one under the other touch.
	float ListRowPitch();

	// How far what stands at a row's end, a date, a count, a status letter or the row's buttons, keeps from the
	// list's right edge.
	float ListRowEndInset();

	// Where a row's end stands in the current list: its visible right edge, clear of a scrollbar, less the inset.
	float ListRowEnd();

	// The same, as the window's own x, for a row laid out with SetCursorPos.
	float ListRowEndPosX();

	// The list's own edges, left to right, whatever the row's indent and however far the list is scrolled sideways.
	float ListLeft();
	float ListRight();

	// A Selectable for the row at the cursor, from the list's left edge to its right whatever the indent, one line of
	// text high. The cursor then goes on as after any Selectable; a row drawn over it goes back to where it started.
	bool ListRowSelectable(const char* id, bool selected, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None);

	// The highlight a row whose text starts at `textTop` takes, square, across the whole list and its pitch high.
	ImRect ListRowHighlightRect(float textTop);

	// The cell padding of a table whose rows are list rows: half the item spacing above a line of text and half
	// below, so a row is a list row's pitch. Push it from before BeginTable to after EndTable.
	ImVec2 ListTableCellPadding();
}
