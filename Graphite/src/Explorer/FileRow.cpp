#include "Explorer/FileRow.h"

#include "UI/FileIcons.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"

#include "Core/ImGui/UiScale.h"
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
		constexpr float kRowMarkerColumnWidth = 12.0f;

		constexpr float kLeastLabelInFontSizes = 3.0f;
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

		bool MarkerHasRoomForOneLetterOfLabel(float labelLeftEdge)
		{
			const float markerColumn = RowMarkerColumnWidth();
			const float markerLeftEdge = MarkerRight() - markerColumn;
			const float roomForOneLetter = markerColumn;

			return markerLeftEdge - ImGui::GetStyle().ItemSpacing.x - labelLeftEdge >= roomForOneLetter;
		}

		// WARNING: A mark drawn on the row is no item of its own, so its tooltip goes by the row's hover
		// and the mouse's place on it.
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

			const float chevronBoxMiddle = layout.RowAt.x + TreeRowChevronBoxLeft() + TreeRowChevronBoxWidth() * kHalf;
			const ImVec2 centre(chevronBoxMiddle, layout.LabelAt.y + ImGui::GetFontSize() * kHalf);

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

		// WARNING: The icon is drawn only whole: a row too narrow for it shows neither the icon nor the name. The
		// name starts past the icon's slot either way, so names line up whatever their icon.
		float DrawLabelIcon(const FileRowLayout& layout, const RowIcon& icon, float labelRightEdge)
		{
			const bool iconFits = layout.LabelAt.x + FileIconWidth() <= labelRightEdge;

			if (iconFits)
			{
				DrawRowIcon(icon, layout.LabelAt);
			}

			return layout.LabelAt.x + IconToNameOffset();
		}

		void DrawNameUpTo(ImVec2 nameAt, float nameRightEdge, const std::string& name, const ImVec2& fullNameSize)
		{
			const ImVec2 nameBottomRight(nameRightEdge, nameAt.y + fullNameSize.y);

			ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), nameAt, nameBottomRight, nameRightEdge, name.c_str(),
									  nullptr, &fullNameSize);
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
		return Core::Scaled(kRowMarkerColumnWidth);
	}

	FileRowLayout LayoutFileRow()
	{
		const float labelOffset = TreeRowLabelOffset();
		const ImVec2 cursor = ImGui::GetCursorScreenPos();

		return FileRowLayout{.RowAt = cursor, .LabelAt = ImVec2(cursor.x + labelOffset, ListRowTextTop(cursor.y))};
	}

	float DrawRowMarker(const RowStatus& status, const FileRowLayout& layout)
	{
		const float right = MarkerRight();

		if (!status.HasMarker() || !MarkerHasRoomForOneLetterOfLabel(layout.LabelAt.x))
		{
			return right;
		}

		const float textTop = layout.LabelAt.y;

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

	void DrawTreeRowLabel(const FileRowLayout& layout, const RowIcon& icon, const std::string& name, bool hasMarker,
						  const std::string& note)
	{
		if (!ImGui::IsItemVisible())
		{
			return;
		}

		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float labelLeftEdge = layout.LabelAt.x;
		float rowRightEdge = MarkerRight();

		if (hasMarker && MarkerHasRoomForOneLetterOfLabel(labelLeftEdge))
		{
			rowRightEdge -= RowMarkerColumnWidth() + spacing;
		}

		rowRightEdge = std::max(rowRightEdge, labelLeftEdge);

		const float nameLeftEdge = DrawLabelIcon(layout, icon, rowRightEdge);

		if (nameLeftEdge >= rowRightEdge)
		{
			return;
		}

		const ImVec2 fullNameSize = ImGui::CalcTextSize(name.c_str());
		const float noteRoom = note.empty() ? 0.0f : ImGui::CalcTextSize(note.c_str()).x + spacing;
		const float leastName = std::min(fullNameSize.x, ImGui::GetFontSize() * kLeastLabelInFontSizes);
		const bool noteFits = noteRoom > 0.0f && rowRightEdge - nameLeftEdge - noteRoom >= leastName;
		const float nameRightEdge = noteFits ? rowRightEdge - noteRoom : rowRightEdge;

		const ImVec2 nameAt(nameLeftEdge, layout.LabelAt.y);
		DrawNameUpTo(nameAt, nameRightEdge, name, fullNameSize);

		if (!noteFits)
		{
			return;
		}

		const float drawnNameRight = std::min(nameLeftEdge + fullNameSize.x, nameRightEdge);
		const ImVec2 noteTopLeft(drawnNameRight + spacing, layout.LabelAt.y);
		const ImVec2 noteBottomRight(rowRightEdge, layout.LabelAt.y + fullNameSize.y);

		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::RenderTextClipped(noteTopLeft, noteBottomRight, note.c_str(), nullptr, nullptr);
		ImGui::PopStyleColor();
	}

	void DrawFileRowLabel(const FileRowLayout& layout, const FileRowLook& look, const std::string& name)
	{
		if (!ImGui::IsItemVisible())
		{
			return;
		}

		DrawOpenTabMark(layout, look.Tab);

		float labelRightEdge = MarkerRight();

		if (look.Status.HasMarker())
		{
			labelRightEdge = DrawRowMarker(look.Status, layout) - ImGui::GetStyle().ItemSpacing.x;
		}

		labelRightEdge = std::max(labelRightEdge, layout.LabelAt.x);

		const float nameLeftEdge = DrawLabelIcon(layout, look.Icon, labelRightEdge);

		if (nameLeftEdge >= labelRightEdge)
		{
			return;
		}

		const ImVec2 nameAt(nameLeftEdge, layout.LabelAt.y);

		ImGui::PushStyleColor(ImGuiCol_Text, look.Ink);
		DrawNameUpTo(nameAt, labelRightEdge, name, ImGui::CalcTextSize(name.c_str()));
		ImGui::PopStyleColor();
	}
}
