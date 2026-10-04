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
	}

	RowStatus FileRowStatusOf(const GitDecorationView& view, const std::filesystem::path& absolute)
	{
		RowStatus status;
		status.Decoration = view.FileOf(absolute);

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
		status.Decoration = view.FolderOf(absolute);

		if (status.Decoration == GitDecoration::None)
		{
			return status;
		}

		status.Ink = view.ColourFor(status.Decoration);

		if (view.IsUntrackedFolderOf(absolute))
		{
			status.Letter = LetterOf(GitDecoration::Untracked);
		}
		else
		{
			status.Dot = true;
		}

		return status;
	}

	FileRowLayout LayoutFileRow(const std::string& label)
	{
		const float labelOffset = ImGui::GetTreeNodeToLabelSpacing();
		const ImVec2 cursor = ImGui::GetCursorScreenPos();

		FileRowLayout layout;
		layout.RowAt = cursor;
		layout.LabelAt = ImVec2(cursor.x + labelOffset, cursor.y);
		layout.Width =
			std::max(ImGui::GetContentRegionAvail().x, labelOffset + ImGui::CalcTextSize(label.c_str()).x);

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
		const float column = ImGui::CalcTextSize("M").x;

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

		if (look.OpenElsewhere)
		{
			// Centred in the gap before the label, where a sibling folder draws its arrow.
			const float gap = layout.LabelAt.x - layout.RowAt.x;
			const ImVec2 centre(layout.RowAt.x + gap * 0.5f, layout.LabelAt.y + ImGui::GetFontSize() * 0.5f);
			drawList->AddCircleFilled(centre, DotRadius(), ImGui::GetColorU32(ImGuiCol_CheckMark));
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
