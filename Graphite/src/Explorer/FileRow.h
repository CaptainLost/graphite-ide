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

	struct FileRowLayout
	{
		ImVec2 RowAt;
		ImVec2 LabelAt;
		float Width = 0.0f;
	};

	struct RowStatus
	{
		GitDecoration Decoration = GitDecoration::None;
		char Letter = '\0';
		bool Dot = false;
		ImU32 Ink = 0;

		bool HasMarker() const { return Letter != '\0' || Dot; }
	};

	RowStatus FileRowStatusOf(const GitDecorationView& view, const std::filesystem::path& absolute,
							  bool ownEntryOnly = false);

	RowStatus FolderRowStatusOf(const GitDecorationView& view, const std::filesystem::path& absolute);

	const char* MarkerTooltipOf(const RowStatus& status);

	enum class OpenTab : std::uint8_t
	{
		None,
		Preview,
		Permanent
	};

	OpenTab OpenTabOf(const Core::DocumentSet& documents, const std::filesystem::path& path);

	const char* OpenTabTooltipOf(OpenTab tab);

	struct FileRowLook
	{
		ImU32 Ink = 0;
		OpenTab Tab = OpenTab::None;

		RowStatus Status;
	};

	// WARNING: Call this at the cursor before the row's Selectable. That item pads its rect out
	// by half the item spacing, so its corner is not where a folder's label would start.
	FileRowLayout LayoutFileRow(const std::string& label, bool reserveMarker);

	float RowMarkerColumnWidth();

	// WARNING: Selectable cannot offset its text from the highlight, so the label is drawn by
	// hand, after the row's Selectable that `layout.Width` sized.
	void DrawFileRowLabel(const FileRowLayout& layout, const FileRowLook& look, const std::string& label);

	// WARNING: Call this while the row's own item is the last one: the marker's tooltip is shown
	// only while that item is hovered.
	float DrawRowMarker(const RowStatus& status, float textTop);
}
