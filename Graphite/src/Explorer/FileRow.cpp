#include "Explorer/FileRow.h"

#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <algorithm>
#include <optional>

namespace Graphite {

	namespace {
		// The dot is drawn rather than typed: a shape needs no glyph in whichever font the tree
		// is drawn in, and comes out round at any interface scale.
		constexpr float kDotRadiusShare = 0.2f;
		constexpr float kRingThicknessShare = 0.08f;
		constexpr float kThinnestRing = 1.0f;

		// AddCircle strokes half a pixel inside the radius it is given and AddCircleFilled does
		// not, so a ring passed the dot's radius comes out half a pixel smaller than the dot.
		constexpr float kAddCircleInset = 0.5f;

		float DotRadius()
		{
			return ImGui::GetFontSize() * kDotRadiusShare;
		}

		float RingThickness()
		{
			return std::max(kThinnestRing, ImGui::GetFontSize() * kRingThicknessShare);
		}

		// The screen x a row's marker ends at. InnerRect, not the content region: the content
		// region's edge (GetWindowContentRegionMax, ContentRegionRect) is shifted by Scroll.x,
		// so a marker placed on it would slide out of view with a sideways scroll. InnerRect is
		// the window's visible inside, already short of a vertical scrollbar.
		float MarkerRight()
		{
			const ImGuiWindow* window = ImGui::GetCurrentWindowRead();
			return window->InnerRect.Max.x - window->WindowPadding.x - ImGui::GetStyle().ItemSpacing.x;
		}

		// A mark drawn on the row is no item of its own, so its tooltip goes by the row's hover
		// and the mouse's place on it. Padded by half the item spacing each way: a dot is a few
		// pixels across and a target that small is hard to rest on.
		void TooltipOver(ImVec2 min, ImVec2 max, const char* text)
		{
			if (text == nullptr || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				return;
			}

			const ImVec2 pad(ImGui::GetStyle().ItemSpacing.x * 0.5f, ImGui::GetStyle().ItemSpacing.y * 0.5f);

			if (ImGui::IsMouseHoveringRect(ImVec2(min.x - pad.x, min.y - pad.y), ImVec2(max.x + pad.x, max.y + pad.y)))
			{
				ImGui::SetTooltip("%s", text);
			}
		}

		const char* DotTooltipOf(GitDecoration decoration)
		{
			switch (decoration)
			{
			case GitDecoration::Untracked: return "Contains untracked files";
			case GitDecoration::Added: return "Contains added files";
			case GitDecoration::Renamed: return "Contains renamed files";
			// A deleted file has no row of its own and raises its folders to Modified.
			case GitDecoration::Modified: return "Contains modified or deleted files";
			case GitDecoration::Conflicted: return "Contains files with conflicts";
			case GitDecoration::None: break;
			}

			return nullptr;
		}

		const char* LetterTooltipOf(GitDecoration decoration)
		{
			switch (decoration)
			{
			case GitDecoration::Untracked: return "Untracked";
			case GitDecoration::Added: return "Added";
			case GitDecoration::Renamed: return "Renamed";
			case GitDecoration::Modified: return "Modified";
			case GitDecoration::Conflicted: return "Conflicted";
			case GitDecoration::None: break;
			}

			return nullptr;
		}

		void DrawOpenTabMark(const FileRowLayout& layout, OpenTab tab)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);

			// Centred in the gap before the label, where a sibling folder draws its arrow.
			const float gap = layout.LabelAt.x - layout.RowAt.x;
			const ImVec2 centre(layout.RowAt.x + gap * 0.5f, layout.LabelAt.y + ImGui::GetFontSize() * 0.5f);

			switch (tab)
			{
			case OpenTab::None:
				return;
			case OpenTab::Preview:
			{
				const float thickness = RingThickness();
				const float radiusEndingAtDotEdge = DotRadius() + kAddCircleInset - thickness * 0.5f;
				drawList->AddCircle(centre, radiusEndingAtDotEdge, accent, 0, thickness);
				break;
			}
			case OpenTab::Permanent:
				drawList->AddCircleFilled(centre, DotRadius(), accent);
				break;
			}

			TooltipOver(ImVec2(layout.RowAt.x, layout.LabelAt.y),
						ImVec2(layout.LabelAt.x, layout.LabelAt.y + ImGui::GetFontSize()), OpenTabTooltipOf(tab));
		}
	}

	const char* MarkerTooltipOf(const RowStatus& status)
	{
		if (status.Dot)
		{
			return DotTooltipOf(status.Decoration);
		}

		// A folder's 'U' stands for the untracked directory it is or lies in, whatever its
		// decoration - see FolderRowStatusOf - so the letter decides it.
		if (status.Letter == LetterOf(GitDecoration::Untracked))
		{
			return LetterTooltipOf(GitDecoration::Untracked);
		}

		return status.Letter != '\0' ? LetterTooltipOf(status.Decoration) : nullptr;
	}

	OpenTab OpenTabOf(const Core::DocumentSet& documents, const std::filesystem::path& path)
	{
		const std::optional<std::size_t> index = documents.Find(path);

		if (!index)
		{
			return OpenTab::None;
		}

		return documents.At(*index).Preview ? OpenTab::Preview : OpenTab::Permanent;
	}

	const char* OpenTabTooltipOf(OpenTab tab)
	{
		switch (tab)
		{
		case OpenTab::Preview: return "Open in a preview tab";
		case OpenTab::Permanent: return "Open in an editor tab";
		case OpenTab::None: break;
		}

		return nullptr;
	}

	RowStatus FileRowStatusOf(const GitDecorationView& view, const std::filesystem::path& absolute,
							  bool ownEntryOnly)
	{
		RowStatus status;
		status.Decoration = ownEntryOnly ? view.OwnFileOf(absolute) : view.FileOf(absolute);

		if (status.Decoration != GitDecoration::None)
		{
			status.Letter = LetterOf(status.Decoration);
			status.Ink = view.ColourFor(status.Decoration);
		}

		return status;
	}

	RowStatus FolderRowStatusOf(const GitDecorationView& view, const std::filesystem::path& absolute)
	{
		RowStatus status;
		const FolderDecoration folder = view.FolderDecorationOf(absolute);
		status.Decoration = folder.Decoration;

		if (status.Decoration == GitDecoration::None)
		{
			return status;
		}

		status.Ink = view.ColourFor(status.Decoration);

		if (folder.Untracked)
		{
			status.Letter = LetterOf(GitDecoration::Untracked);
		}
		else
		{
			status.Dot = true;
		}

		return status;
	}

	float RowMarkerColumnWidth()
	{
		return ImGui::CalcTextSize("M").x;
	}

	FileRowLayout LayoutFileRow()
	{
		const float labelOffset = ImGui::GetTreeNodeToLabelSpacing();
		const ImVec2 cursor = ImGui::GetCursorScreenPos();

		FileRowLayout layout;
		layout.RowAt = cursor;
		layout.LabelAt = ImVec2(cursor.x + labelOffset, cursor.y);
		layout.Width = ImGui::GetContentRegionAvail().x;

		return layout;
	}

	float DrawRowMarker(const RowStatus& status, float textTop)
	{
		const float right = MarkerRight();

		if (!status.HasMarker())
		{
			return right;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		// Dots and letters share one column, as wide as the widest letter, so a folder's dot sits
		// over its files' letters.
		const float column = RowMarkerColumnWidth();

		const float textBottom = textTop + ImGui::GetFontSize();

		TooltipOver(ImVec2(right - column, textTop), ImVec2(right, textBottom), MarkerTooltipOf(status));

		if (status.Dot)
		{
			const ImVec2 centre(right - column * 0.5f, textTop + ImGui::GetFontSize() * 0.5f);
			drawList->AddCircleFilled(centre, DotRadius(), status.Ink);
			return right - column;
		}

		const char text[] = {status.Letter, '\0'};
		const float width = ImGui::CalcTextSize(text).x;
		const float left = right - (column + width) * 0.5f;
		drawList->AddText(ImVec2(left, textTop), status.Ink, text);

		return std::min(left, right - column);
	}

	void DrawFileRowLabel(const FileRowLayout& layout, const FileRowLook& look, const std::string& label)
	{
		if (!ImGui::IsItemVisible())
		{
			return;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		DrawOpenTabMark(layout, look.Tab);

		float labelRightEdge = MarkerRight();

		if (look.Status.HasMarker())
		{
			labelRightEdge = DrawRowMarker(look.Status, layout.LabelAt.y) - ImGui::GetStyle().ItemSpacing.x;
		}

		labelRightEdge = std::max(labelRightEdge, layout.LabelAt.x);

		const ImVec2 fullLabelSize = ImGui::CalcTextSize(label.c_str());
		const ImVec2 labelBottomRight(labelRightEdge, layout.LabelAt.y + fullLabelSize.y);

		ImGui::PushStyleColor(ImGuiCol_Text, look.Ink);
		ImGui::RenderTextEllipsis(drawList, layout.LabelAt, labelBottomRight, labelRightEdge, label.c_str(),
								  nullptr, &fullLabelSize);
		ImGui::PopStyleColor();
	}
}
