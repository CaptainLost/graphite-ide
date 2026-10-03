#pragma once

#include <imgui.h>

#include <string>

namespace Graphite {

	// Where a file row's label goes and how wide its item is. A folder's label starts
	// GetTreeNodeToLabelSpacing right of its arrow; a file has no arrow but keeps the gap, so
	// its icon lines up with its siblings' folders' icons and not with their parent's. The label
	// is drawn by hand because Selectable has no way to offset its text from the highlight.
	struct FileRowLayout
	{
		ImVec2 LabelAt;
		float Width = 0.0f;
	};

	// Taken at the cursor, before the row's Selectable: that item pads its rect out by half the
	// item spacing, so the rect's corner is not where a folder's label would start. The width
	// is what the label needs at least, so a long name still extends the scrollable width.
	FileRowLayout LayoutFileRow(const std::string& label);

	// After the row's Selectable, which `layout.Width` sized. Draws nothing for a row that is
	// scrolled out of view.
	void DrawFileRowLabel(const FileRowLayout& layout, ImU32 ink, const std::string& label);
}
