#include "Editor/Authors/LinePeek.h"

#include "Core/Base/Narrow.h"

#include "Editor/Authors/LinePeekContent.h"
#include "Theme/ColorScheme.h"
#include "Theme/GitPalette.h"
#include "UI/TypographicMarks.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/TextButton.h"
#include "Core/ImGui/UiScale.h"

#include <IconsLucide.h>

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		constexpr float kListWidthEms = 15.0f;
		constexpr float kLinesPerEntry = 2.0f;
		constexpr float kAccentBarWidth = 3.0f;
		constexpr float kDiffTextInset = 6.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kBothSides = 2.0f;
		constexpr float kKeepFontSize = 0.0f;
		constexpr float kNoItemSpacing = 0.0f;
		constexpr float kFillRemainingWidth = 0.0f;
		constexpr float kWrapAtWindowEdge = 0.0f;

		constexpr std::string_view kLoadingHistory = "Loading history";
		constexpr const char* kHistoryStopsHere = "History stops here.";
		constexpr const char* kShowCommit = "Show commit";
		constexpr const char* kCloseLabel = ICON_LC_X "##LinePeekClose";

		struct PeekColours
		{
			GitPalette Git;
			ImU32 Accent = 0;
			ImVec4 Selected{};
		};

		PeekColours ReadPeekColours(const ResolvedScheme& scheme)
		{
			PeekColours colours;
			colours.Git = ReadGitPalette(scheme);
			colours.Accent = ImGui::ColorConvertFloat4ToU32(Core::ToImVec4(ColourFromScheme(scheme, ChromeColor::Accent)));
			colours.Selected = Core::ToImVec4(ColourFromScheme(scheme, ChromeColor::SurfaceActive));

			return colours;
		}

		ImU32 TextColourOf(Core::DiffLineKind kind, const GitPalette& palette)
		{
			switch (kind)
			{
			case Core::DiffLineKind::Added:
				return palette.DiffAddedText;
			case Core::DiffLineKind::Removed:
				return palette.DiffRemovedText;
			case Core::DiffLineKind::Context:
				break;
			}

			return ImGui::GetColorU32(ImGuiCol_Text);
		}

		// The header's text, clipped short of the close button at the row's right end. True when the button was
		// clicked.
		bool DrawHeader(const std::string& header, float width)
		{
			const ImGuiStyle& style = ImGui::GetStyle();

			const float lineHeight = ImGui::GetTextLineHeight();
			const float closeWidth = ImGui::CalcTextSize(ICON_LC_X).x + style.FramePadding.x * kBothSides;
			const float textWidth = std::max(0.0f, width - closeWidth - style.ItemSpacing.x);
			const ImVec2 corner = ImGui::GetCursorScreenPos();

			ImDrawList* const draw = ImGui::GetWindowDrawList();

			draw->PushClipRect(corner, ImVec2(corner.x + textWidth, corner.y + lineHeight), true);
			draw->AddText(corner, ImGui::GetColorU32(ImGuiCol_Text), header.data(), header.data() + header.size());
			draw->PopClipRect();

			ImGui::Dummy(ImVec2(textWidth, lineHeight));
			ImGui::SameLine();

			return Core::SmallTextButton(kCloseLabel);
		}

		// Each entry is two lines on a row as wide as the list, its title and then its meta muted. Returns the entry
		// clicked.
		std::optional<std::size_t> DrawEntries(const LinePeekContent& content, std::size_t selected,
											   const PeekColours& colours)
		{
			if (content.State == PeekState::Loading)
			{
				const std::string loading = std::string{kLoadingHistory} + std::string{kEllipsis};
				ImGui::TextDisabled("%s", loading.c_str());
				return std::nullopt;
			}

			if (content.State == PeekState::Failed)
			{
				ImGui::PushTextWrapPos(kWrapAtWindowEdge);
				ImGui::TextDisabled("%s", content.Error.c_str());
				ImGui::PopTextWrapPos();
				return std::nullopt;
			}

			const float lineHeight = ImGui::GetTextLineHeight();
			const float inset = ImGui::GetStyle().FramePadding.x;
			const ImU32 titleColour = ImGui::GetColorU32(ImGuiCol_Text);
			const ImU32 metaColour = ImGui::GetColorU32(ImGuiCol_TextDisabled);

			std::optional<std::size_t> clicked;

			for (std::size_t index = 0; index < content.Entries.size(); ++index)
			{
				const PeekEntry& entry = content.Entries[index];
				const bool isSelected = index == selected;
				const std::string id = std::format("##LinePeekEntry{}", index);

				if (isSelected)
				{
					ImGui::PushStyleColor(ImGuiCol_Header, colours.Selected);
				}

				if (ImGui::Selectable(id.c_str(), isSelected, ImGuiSelectableFlags_None,
									  ImVec2(kFillRemainingWidth, lineHeight * kLinesPerEntry)))
				{
					clicked = index;
				}

				if (isSelected)
				{
					ImGui::PopStyleColor();
				}

				if (!ImGui::IsItemVisible())
				{
					continue;
				}

				const ImVec2 min = ImGui::GetItemRectMin();
				const ImVec2 max = ImGui::GetItemRectMax();

				ImDrawList* const draw = ImGui::GetWindowDrawList();

				draw->PushClipRect(min, max, true);
				draw->AddText(ImVec2(min.x + inset, min.y), titleColour, entry.Title.c_str());
				draw->AddText(ImVec2(min.x + inset, min.y + lineHeight), metaColour, entry.Meta.c_str());
				draw->PopClipRect();
			}

			if (content.StopsEarly)
			{
				ImGui::TextDisabled("%s", kHistoryStopsHere);
			}

			return clicked;
		}

		// A band in the Git diff's colours as wide as the view or the text, whichever is wider, with the accent bar on
		// the line the peek follows.
		void DrawDiffLine(const PeekDiffLine& line, const PeekColours& colours)
		{
			const ImVec2 corner = ImGui::GetCursorScreenPos();
			const float lineHeight = ImGui::GetTextLineHeight();
			const float barWidth = Core::Scaled(kAccentBarWidth);
			const float textLeft = corner.x + barWidth + Core::Scaled(kDiffTextInset);
			const char* const textEnd = line.Text.data() + line.Text.size();
			const float textRight = textLeft + ImGui::CalcTextSize(line.Text.data(), textEnd).x;
			const float right = std::max(corner.x + ImGui::GetContentRegionAvail().x, textRight);

			ImDrawList* const draw = ImGui::GetWindowDrawList();

			if (line.Kind != Core::DiffLineKind::Context)
			{
				const ImU32 band =
					line.Kind == Core::DiffLineKind::Added ? colours.Git.DiffAddedLine : colours.Git.DiffRemovedLine;

				draw->AddRectFilled(corner, ImVec2(right, corner.y + lineHeight), band);
			}

			if (line.Tracked)
			{
				draw->AddRectFilled(corner, ImVec2(corner.x + barWidth, corner.y + lineHeight), colours.Accent);
			}

			draw->AddText(ImVec2(textLeft, corner.y), TextColourOf(line.Kind, colours.Git), line.Text.data(), textEnd);

			ImGui::Dummy(ImVec2(right - corner.x, lineHeight));
		}

		// True when Show commit was clicked.
		bool DrawEntryDetail(const PeekEntry& entry, float codeSize, const PeekColours& colours)
		{
			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			ImGui::TextUnformatted(entry.Title.c_str());
			ImGui::PopFont();

			ImGui::TextDisabled("%s", entry.Byline.c_str());

			// The bands of a hunk touch, as they do in the Git diff.
			const float spacingX = ImGui::GetStyle().ItemSpacing.x;

			Core::PushCodeFont(codeSize);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacingX, kNoItemSpacing));

			for (const PeekDiffLine& line : entry.Lines)
			{
				DrawDiffLine(line, colours);
			}

			ImGui::PopStyleVar();
			ImGui::PopFont();

			if (!entry.Commit.has_value())
			{
				return false;
			}

			ImGui::Spacing();

			return Core::TextButton(kShowCommit);
		}
	}

	std::optional<PeekAction> DrawLinePeek(const LinePeekContent& content, std::size_t selected, ImVec2 size,
										   const ResolvedScheme& scheme)
	{
		const float codeSize = ImGui::GetFontSize();
		const PeekColours colours = ReadPeekColours(scheme);
		const std::size_t shown = selected < content.Entries.size() ? selected : 0;

		std::optional<PeekAction> answer;

		Core::PushInterfaceFont();

		const float top = ImGui::GetCursorPosY();

		if (DrawHeader(content.Header, size.x))
		{
			answer = PeekAction{.Action = PeekAction::Kind::Close};
		}

		const float bodyHeight = std::max(0.0f, size.y - (ImGui::GetCursorPosY() - top));
		const float listWidth = std::min(kListWidthEms * ImGui::GetFontSize(), size.x * kHalf);

		if (ImGui::BeginChild("##LinePeekList", ImVec2(listWidth, bodyHeight)))
		{
			if (const std::optional<std::size_t> clicked = DrawEntries(content, shown, colours))
			{
				answer = PeekAction{.Action = PeekAction::Kind::Select, .Entry = *clicked};
			}
		}

		ImGui::EndChild();
		ImGui::SameLine();

		// A child per entry, so each opens at its own top rather than at the scroll the previous one was left at.
		ImGui::PushID(Core::Narrow<int>(shown));

		if (ImGui::BeginChild("##LinePeekDiff", ImVec2(kFillRemainingWidth, bodyHeight), ImGuiChildFlags_None,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			if (content.State == PeekState::Ready && shown < content.Entries.size() &&
				DrawEntryDetail(content.Entries[shown], codeSize, colours))
			{
				answer = PeekAction{.Action = PeekAction::Kind::ShowCommit, .Entry = shown};
			}
		}

		ImGui::EndChild();
		ImGui::PopID();

		ImGui::PopFont();

		return answer;
	}

	bool PeekClosedBy(const PeekFrame& frame)
	{
		return frame.CaretLine != frame.PeekLine || frame.Edited || frame.CloseRequested;
	}
}
