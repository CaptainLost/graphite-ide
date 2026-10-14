#pragma once

#include "Git/GitDecorationView.h"

#include <imgui.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace Core {

	class DocumentSet;

}

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
	// With `ownEntryOnly`, what a file merely inherits from an untracked directory around it is
	// left out; ignored rows use it, since a tracked file can sit under an ignore pattern.
	RowStatus FileRowStatusOf(const GitDecorationView& view, const std::filesystem::path& absolute,
							  bool ownEntryOnly = false);

	// A folder's: a dot in the colour of the highest state beneath it, or 'U' for a directory git
	// reports as untracked and for every folder inside one. The folders above an untracked
	// directory get the dot.
	RowStatus FolderRowStatusOf(const GitDecorationView& view, const std::filesystem::path& absolute);

	// What the marker means in words, shown when the mouse rests on it: the state a letter stands
	// for, or what a dot says lies beneath its folder. Null for a row with no marker.
	const char* MarkerTooltipOf(const RowStatus& status);

	enum class OpenTab : std::uint8_t
	{
		None,
		Preview,
		Permanent
	};

	OpenTab OpenTabOf(const Core::DocumentSet& documents, const std::filesystem::path& path);

	// Null for a file open in no tab.
	const char* OpenTabTooltipOf(OpenTab tab);

	// How a file row's label is drawn.
	struct FileRowLook
	{
		// The name's colour.
		ImU32 Ink = 0;

		// Marked in the accent colour in the gap left of the label, where a folder has its arrow,
		// so the label itself does not move: a filled dot for a permanent tab, a ring for the preview.
		OpenTab Tab = OpenTab::None;

		RowStatus Status;
	};

	// Taken at the cursor, before the row's Selectable: that item pads its rect out by half the
	// item spacing, so the rect's corner is not where a folder's label would start. The width is
	// the room the window has left: the trees do not scroll sideways, a long name is cut short.
	FileRowLayout LayoutFileRow();

	// The width of the column a row's marker is drawn in, as wide as the widest letter.
	float RowMarkerColumnWidth();

	// After the row's Selectable, which `layout.Width` sized. Draws nothing for a row that is
	// scrolled out of view. A name too long for the row ends in an ellipsis short of the status letter.
	// The open-tab mark and the marker each explain themselves in a tooltip when the mouse
	// rests on them, and only then, so the rest of the row stays quiet.
	void DrawFileRowLabel(const FileRowLayout& layout, const FileRowLook& look, const std::string& label);

	// The label of a tree row whose arrow `TreeNodeEx` has just drawn with an empty label, in the
	// text colour in force. A name too long for the row ends in an ellipsis short of the marker
	// when `hasMarker` says one is drawn on the row, and short of the row's edge otherwise.
	// Call it while the row's own item is the last one, before the row's marker is drawn.
	void DrawTreeRowLabel(const std::string& label, bool hasMarker);

	// Draws `status`'s marker with its right edge at the visible right edge of the current
	// window, on the line of text whose top is `textTop`. The edge stays put while the window
	// scrolls sideways, so the marker is always in view. Returns the marker's left edge, or the
	// right edge itself when there is no marker. Call it while the row's own item is the last one:
	// the marker's tooltip is shown only while that item is hovered.
	float DrawRowMarker(const RowStatus& status, float textTop);
}
