#include "Explorer/FileRow.h"

#include "Core/Text/DocumentSet.h"

#include <imgui_internal.h>

#include <algorithm>
#include <optional>

namespace Graphite {

	namespace {
		constexpr float kDotRadiusShare = 0.2f;
		constexpr float kRingThicknessShare = 0.08f;
		constexpr float kThinnestRing = 1.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kGapsAroundMarker = 2.0f;
		constexpr int kSegmentsChosenByImGui = 0;
		constexpr bool kIntersectWithCurrentClipRect = true;

		// WARNING: AddCircle strokes half a pixel inside the radius it is given and
		// AddCircleFilled does not, so a ring passed the dot's radius comes out smaller than the dot.
		constexpr float kAddCircleInset = 0.5f;

		float DotRadius()
		{
			return ImGui::GetFontSize() * kDotRadiusShare;
		}

		float RingThickness()
		{
			return std::max(kThinnestRing, ImGui::GetFontSize() * kRingThicknessShare);
		}

		// WARNING: The content region's edge is shifted by Scroll.x, so a marker placed on it would
		// slide out of view with a sideways scroll. InnerRect is the visible inside of the window.
		float MarkerRight()
		{
			const ImGuiWindow* window = ImGui::GetCurrentWindowRead();
			return window->InnerRect.Max.x - window->WindowPadding.x - ImGui::GetStyle().ItemSpacing.x;
		}

		void TooltipOver(ImVec2 rectMin, ImVec2 rectMax, const char* text)
		{
			if (text == nullptr || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				return;
			}

			const ImVec2 hoverPadding(ImGui::GetStyle().ItemSpacing.x * kHalf, ImGui::GetStyle().ItemSpacing.y * kHalf);
			const ImVec2 paddedMin(rectMin.x - hoverPadding.x, rectMin.y - hoverPadding.y);
			const ImVec2 paddedMax(rectMax.x + hoverPadding.x, rectMax.y + hoverPadding.y);

			if (ImGui::IsMouseHoveringRect(paddedMin, paddedMax))
			{
				ImGui::SetTooltip("%s", text);
			}
		}

		const char* DotTooltipOf(GitDecoration decoration)
		{
			switch (decoration)
			{
			case GitDecoration::Untracked:
				return "Contains untracked files";
			case GitDecoration::Added:
				return "Contains added files";
			case GitDecoration::Renamed:
				return "Contains renamed files";
			case GitDecoration::Modified:
				return "Contains modified or deleted files";
			case GitDecoration::Conflicted:
				return "Contains files with conflicts";
			case GitDecoration::None:
				break;
			}

			return nullptr;
		}

		const char* LetterTooltipOf(GitDecoration decoration)
		{
			switch (decoration)
			{
			case GitDecoration::Untracked:
				return "Untracked";
			case GitDecoration::Added:
				return "Added";
			case GitDecoration::Renamed:
				return "Renamed";
			case GitDecoration::Modified:
				return "Modified";
			case GitDecoration::Conflicted:
				return "Conflicted";
			case GitDecoration::None:
				break;
			}

			return nullptr;
		}

		void DrawOpenTabMark(const FileRowLayout& layout, OpenTab tab)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);

			const float gap = layout.LabelAt.x - layout.RowAt.x;
			const ImVec2 centre(layout.RowAt.x + gap * kHalf, layout.LabelAt.y + ImGui::GetFontSize() * kHalf);

			switch (tab)
			{
			case OpenTab::None:
				return;
			case OpenTab::Preview:
			{
				const float thickness = RingThickness();
				const float radiusEndingAtDotEdge = DotRadius() + kAddCircleInset - thickness * kHalf;
				drawList->AddCircle(centre, radiusEndingAtDotEdge, accent, kSegmentsChosenByImGui, thickness);
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

		const bool isLetterOfUntrackedDirectory = status.Letter == LetterOf(GitDecoration::Untracked);

		if (isLetterOfUntrackedDirectory)
		{
			return LetterTooltipOf(GitDecoration::Untracked);
		}

		return status.Letter != '\0' ? LetterTooltipOf(status.Decoration) : nullptr;
	}

	OpenTab OpenTabOf(const Core::DocumentSet& documents, const std::filesystem::path& path)
	{
		const std::optional<std::size_t> documentIndex = documents.Find(path);

		if (!documentIndex)
		{
			return OpenTab::None;
		}

		return documents.At(*documentIndex).Preview ? OpenTab::Preview : OpenTab::Permanent;
	}

	const char* OpenTabTooltipOf(OpenTab tab)
	{
		switch (tab)
		{
		case OpenTab::Preview:
			return "Open in a preview tab";
		case OpenTab::Permanent:
			return "Open in an editor tab";
		case OpenTab::None:
			break;
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

	FileRowLayout LayoutFileRow(const std::string& label, bool reserveMarker)
	{
		const float labelOffset = ImGui::GetTreeNodeToLabelSpacing();
		const ImVec2 cursor = ImGui::GetCursorScreenPos();

		const float markerRoom =
			reserveMarker ? RowMarkerColumnWidth() + kGapsAroundMarker * ImGui::GetStyle().ItemSpacing.x : 0.0f;

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

		const float columnWidth = RowMarkerColumnWidth();

		const float textBottom = textTop + ImGui::GetFontSize();

		TooltipOver(ImVec2(right - columnWidth, textTop), ImVec2(right, textBottom), MarkerTooltipOf(status));

		if (status.Dot)
		{
			const ImVec2 centre(right - columnWidth * kHalf, textTop + ImGui::GetFontSize() * kHalf);
			drawList->AddCircleFilled(centre, DotRadius(), status.Ink);
			return right - columnWidth;
		}

		const char letterText[] = {status.Letter, '\0'};
		const float letterWidth = ImGui::CalcTextSize(letterText).x;
		const float left = right - (columnWidth + letterWidth) * kHalf;
		drawList->AddText(ImVec2(left, textTop), status.Ink, letterText);

		return std::min(left, right - columnWidth);
	}

	void DrawFileRowLabel(const FileRowLayout& layout, const FileRowLook& look, const std::string& label)
	{
		if (!ImGui::IsItemVisible())
		{
			return;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		DrawOpenTabMark(layout, look.Tab);

		if (!look.Status.HasMarker())
		{
			drawList->AddText(layout.LabelAt, look.Ink, label.c_str());
			return;
		}

		const float markerLeft = DrawRowMarker(look.Status, layout.LabelAt.y);

		const ImVec2 clipMin = drawList->GetClipRectMin();
		const ImVec2 clipMax(markerLeft - ImGui::GetStyle().ItemSpacing.x, drawList->GetClipRectMax().y);

		ImGui::PushClipRect(clipMin, ImVec2(std::max(clipMin.x, clipMax.x), clipMax.y), kIntersectWithCurrentClipRect);
		drawList->AddText(layout.LabelAt, look.Ink, label.c_str());
		ImGui::PopClipRect();
	}
}
