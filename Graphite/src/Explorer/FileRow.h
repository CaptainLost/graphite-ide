#pragma once

#include "Git/GitDecorationView.h"
#include "UI/FileIcons.h"

#include "Core/ImGui/LoadingState.h"

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
		RowIcon Icon;

		RowStatus Status;
	};

	// WARNING: Call this at the cursor before the row's item, a file's or a folder's. That item is the whole list's
	// width from its left edge, whatever the row's indent, so its corner is not where the row's label starts.
	FileRowLayout LayoutFileRow();

	float RowMarkerColumnWidth();

	// WARNING: Selectable cannot offset its text from the highlight, so the label is drawn by
	// hand, after the row's ListRowSelectable.
	void DrawFileRowLabel(const FileRowLayout& layout, const FileRowLook& look, const std::string& name);

	// WARNING: Call this while the row's own item is the last one, before the row's marker is drawn.
	void DrawTreeRowLabel(const FileRowLayout& layout, const RowIcon& icon, const std::string& name, bool hasMarker,
						  const std::string& note = {});

	// WARNING: Call this while the row's own item is the last one: the marker's tooltip is shown
	// only while that item is hovered.
	float DrawRowMarker(const RowStatus& status, const FileRowLayout& layout);

	bool DrawTreeLoader(const std::string& text, Core::WhileLoaderWaits whileWaiting);
}
