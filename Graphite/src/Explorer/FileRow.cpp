#include "Explorer/FileRow.h"

#include <imgui_internal.h>

#include <algorithm>

namespace Graphite {

	namespace {
		// The dot is drawn rather than typed: a shape needs no glyph in whichever font the tree
		// is drawn in, and comes out round at any interface scale.
		constexpr float kDotRadiusShare = 0.2f;

		float DotRadius()
		{
			return ImGui::GetFontSize() * kDotRadiusShare;
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

	FileRowLayout LayoutFileRow(const std::string& label, bool reserveMarker)
	{
		const float labelOffset = ImGui::GetTreeNodeToLabelSpacing();
		const ImVec2 cursor = ImGui::GetCursorScreenPos();

		// The marker sits at the visible right edge, with a gap each side of it to the label and to
		// the edge, so a name scrolled fully into view still ends short of it.
		const float markerRoom =
			reserveMarker ? RowMarkerColumnWidth() + 2.0f * ImGui::GetStyle().ItemSpacing.x : 0.0f;

		FileRowLayout layout;
		layout.RowAt = cursor;
		layout.LabelAt = ImVec2(cursor.x + labelOffset, cursor.y);
		layout.Width = std::max(ImGui::GetContentRegionAvail().x,
								labelOffset + ImGui::CalcTextSize(label.c_str()).x + markerRoom);

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

		if (look.IsOpenInTab)
		{
			// Centred in the gap before the label, where a sibling folder draws its arrow.
			const float gap = layout.LabelAt.x - layout.RowAt.x;
			const ImVec2 centre(layout.RowAt.x + gap * 0.5f, layout.LabelAt.y + ImGui::GetFontSize() * 0.5f);
			drawList->AddCircleFilled(centre, DotRadius(), ImGui::GetColorU32(ImGuiCol_CheckMark));

			TooltipOver(ImVec2(layout.RowAt.x, layout.LabelAt.y),
						ImVec2(layout.LabelAt.x, layout.LabelAt.y + ImGui::GetFontSize()), "Open in an editor tab");
		}

		if (!look.Status.HasMarker())
		{
			drawList->AddText(layout.LabelAt, look.Ink, label.c_str());
			return;
		}

		const float markerLeft = DrawRowMarker(look.Status, layout.LabelAt.y);

		// Clipped short of the letter rather than drawn under it. Intersected with the window's
		// own clip rect, so the label never draws outside the tree either.
		const ImVec2 clipMin = drawList->GetClipRectMin();
		const ImVec2 clipMax(markerLeft - ImGui::GetStyle().ItemSpacing.x, drawList->GetClipRectMax().y);

		ImGui::PushClipRect(clipMin, ImVec2(std::max(clipMin.x, clipMax.x), clipMax.y), true);
		drawList->AddText(layout.LabelAt, look.Ink, label.c_str());
		ImGui::PopClipRect();
	}
}
