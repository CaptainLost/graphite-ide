#pragma once

#include "Git/GitDecorationView.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace Graphite {

	// Where a file row's label goes and how wide its item is. A folder's label starts
	// GetTreeNodeToLabelSpacing right of its arrow; a file has no arrow but keeps the gap, so
	// its icon lines up with its siblings' folders' icons and not with their parent's. The label
	// is drawn by hand because Selectable has no way to offset its text from the highlight.
	struct FileRowLayout
	{
		// Where the row starts: the left edge of the gap a folder's arrow would occupy.
		ImVec2 RowAt;
		ImVec2 LabelAt;
		float Width = 0.0f;
	};

	// What git says about one row: the decoration, the colour it is drawn in, and the marker
	// right-aligned on the row - a status letter, or a dot for a folder with a change beneath it.
	struct RowStatus
	{
		GitDecoration Decoration = GitDecoration::None;
		char Letter = '\0';
		bool Dot = false;
		ImU32 Ink = 0;

		bool HasMarker() const { return Letter != '\0' || Dot; }
	};

	// A file's own status: its letter, in its state's colour. Nothing outside the repository.
	RowStatus FileRowStatusOf(const GitDecorationView& view, const std::filesystem::path& absolute);

	// A folder's: a dot in the colour of the highest state beneath it, or 'U' for a directory git
	// reports as untracked and for every folder inside one. The folders above an untracked
	// directory get the dot.
	RowStatus FolderRowStatusOf(const GitDecorationView& view, const std::filesystem::path& absolute);

	// How a file row's label is drawn.
	struct FileRowLook
	{
		// The name's colour.
		ImU32 Ink = 0;

		// Whether the file is open in a tab that is not the active one. Marked by a dot in the
		// accent colour in the gap left of the label, where a folder has its arrow, so the label
		// itself does not move.
		bool OpenElsewhere = false;

		RowStatus Status;
	};

	// Taken at the cursor, before the row's Selectable: that item pads its rect out by half the
	// item spacing, so the rect's corner is not where a folder's label would start. The width
	// is what the label needs at least, so a long name still extends the scrollable width.
	FileRowLayout LayoutFileRow(const std::string& label);

	// After the row's Selectable, which `layout.Width` sized. Draws nothing for a row that is
	// scrolled out of view. A name long enough to reach the status letter is clipped short of it.
	void DrawFileRowLabel(const FileRowLayout& layout, const FileRowLook& look, const std::string& label);

	// Draws `status`'s marker with its right edge at the visible right edge of the current
	// window, on the line of text whose top is `textTop`. The edge stays put while the window
	// scrolls sideways, so the marker is always in view. Returns the marker's left edge, or the
	// right edge itself when there is no marker.
	float DrawRowMarker(const RowStatus& status, float textTop);
}
