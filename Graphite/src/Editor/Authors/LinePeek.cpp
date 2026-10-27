#include "Editor/Authors/LinePeek.h"

#include "Editor/Authors/LinePeekContent.h"
#include "Theme/ColorScheme.h"
#include "Theme/GitPalette.h"
#include "UI/FittedText.h"
#include "UI/TypographicMarks.h"

#include "Core/App/Redraw.h"
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
		constexpr float kSelectedBarWidth = 2.0f;
		constexpr float kDiffTextInset = 6.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kBothSides = 2.0f;
		constexpr float kKeepFontSize = 0.0f;
		constexpr float kNoItemSpacing = 0.0f;
		constexpr float kFillRemainingWidth = 0.0f;
		constexpr float kWrapAtWindowEdge = 0.0f;
		constexpr int kNothingShown = 0;

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

		// The history icon, the title in the semibold face and the counts muted, clipped short of the close button at the
		// row's right end. True when the button was clicked.
		bool DrawHeader(const LinePeekContent& content, float width)
		{
			const ImGuiStyle& style = ImGui::GetStyle();

			const float lineHeight = ImGui::GetTextLineHeight();
			const float closeWidth = ImGui::CalcTextSize(ICON_LC_X).x + style.FramePadding.x * kBothSides;
			const float textWidth = std::max(0.0f, width - closeWidth - style.ItemSpacing.x);
			const float gap = style.ItemSpacing.x;
			const ImVec2 corner = ImGui::GetCursorScreenPos();

			ImDrawList* const draw = ImGui::GetWindowDrawList();
			float x = corner.x;

			draw->PushClipRect(corner, ImVec2(corner.x + textWidth, corner.y + lineHeight), true);

			draw->AddText(ImVec2(x, corner.y), ImGui::GetColorU32(ImGuiCol_Text), ICON_LC_HISTORY);
			x += ImGui::CalcTextSize(ICON_LC_HISTORY).x + gap;

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			draw->AddText(ImVec2(x, corner.y), ImGui::GetColorU32(ImGuiCol_Text), content.Header.c_str());
			x += ImGui::CalcTextSize(content.Header.c_str()).x + gap;
			ImGui::PopFont();

			if (!content.Counts.empty())
			{
				draw->AddText(ImVec2(x, corner.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), content.Counts.c_str());
			}

			draw->PopClipRect();

			ImGui::Dummy(ImVec2(textWidth, lineHeight));
			ImGui::SameLine();

			return Core::SmallTextButton(kCloseLabel);
		}

		// Each entry is two lines on a row as wide as the list, its title and then its meta muted, each cut with an
		// ellipsis to the row; the selected one has the accent bar at its left. Returns the entry clicked.
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
				const float room = std::max(0.0f, max.x - min.x - inset * kBothSides);

				// WARNING: Cut to fit, not clipped: a clip took the end of the meta line, which is the short hash.
				const std::string title = FitToWidth(entry.Title, room);
				const std::string meta = FitToWidth(entry.Meta, room);

				ImDrawList* const draw = ImGui::GetWindowDrawList();

				if (isSelected)
				{
					draw->AddRectFilled(min, ImVec2(min.x + Core::Scaled(kSelectedBarWidth), max.y), colours.Accent);
				}

				draw->PushClipRect(min, max, true);
				draw->AddText(ImVec2(min.x + inset, min.y), titleColour, title.c_str());
				draw->AddText(ImVec2(min.x + inset, min.y + lineHeight), metaColour, meta.c_str());
				draw->PopClipRect();
			}

			if (content.StopsEarly)
			{
				ImGui::TextDisabled("%s", kHistoryStopsHere);
			}

			return clicked;
		}

		// A band in the Git diff's colours as wide as the view or the text, whichever is wider, with the accent bar on
		// the line the peek follows. A row standing for lines left out is its ellipsis alone, muted. Returns how far
		// the row's text reaches from its left edge.
		float DrawDiffLine(const PeekDiffLine& line, const PeekColours& colours)
		{
			const ImVec2 corner = ImGui::GetCursorScreenPos();
			const float lineHeight = ImGui::GetTextLineHeight();
			const float barWidth = Core::Scaled(kAccentBarWidth);
			const float textLeft = corner.x + barWidth + Core::Scaled(kDiffTextInset);
			const char* const textEnd = line.Text.data() + line.Text.size();

			if (line.Omitted)
			{
				const float reach = textLeft - corner.x + ImGui::CalcTextSize(line.Text.data(), textEnd).x;

				ImGui::GetWindowDrawList()->AddText(ImVec2(textLeft, corner.y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
													line.Text.data(), textEnd);
				ImGui::Dummy(ImVec2(reach, lineHeight));
				return reach;
			}

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
			return textRight - corner.x;
		}

		// What the detail drew, measured in its own content coordinates, which do not move with its scroll.
		struct DetailLayout
		{
			bool ShowCommit = false;

			std::optional<float> TrackedTop;   // the first tracked row's
			float TrackedBottom = 0.0f;        // the last tracked row's

			float Bottom = 0.0f;
			float WidestRow = 0.0f;
		};

		// The title, the byline with Show commit at its end, then the diff.
		DetailLayout DrawEntryDetail(const PeekEntry& entry, float codeSize, const PeekColours& colours)
		{
			DetailLayout layout;

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), kKeepFontSize);
			ImGui::TextUnformatted(entry.Title.c_str());
			ImGui::PopFont();

			// WARNING: Beside the byline, not under the diff. Under it the button sat below the fold of every entry with
			// more than a few lines, reachable only by scrolling.
			if (entry.Commit.has_value())
			{
				ImGui::AlignTextToFramePadding();
			}

			ImGui::TextDisabled("%s", entry.Byline.c_str());

			if (entry.Commit.has_value())
			{
				ImGui::SameLine();
				layout.ShowCommit = Core::TextButton(kShowCommit);
			}

			// The bands of a hunk touch, as they do in the Git diff.
			const float spacingX = ImGui::GetStyle().ItemSpacing.x;

			Core::PushCodeFont(codeSize);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacingX, kNoItemSpacing));

			const float rowHeight = ImGui::GetTextLineHeight();

			for (const PeekDiffLine& line : entry.Lines)
			{
				const float rowTop = ImGui::GetCursorPosY();

				layout.WidestRow = std::max(layout.WidestRow, DrawDiffLine(line, colours));

				if (line.Tracked)
				{
					layout.TrackedTop = layout.TrackedTop.value_or(rowTop);
					layout.TrackedBottom = rowTop + rowHeight;
				}
			}

			layout.Bottom = ImGui::GetCursorPosY();

			ImGui::PopStyleVar();
			ImGui::PopFont();

			return layout;
		}

		// How far down a view `viewHeight` tall is scrolled to show rows from `top` to `bottom`: not at all when they
		// fit below the title and byline, else just far enough to show the last of them, but never past the first.
		float ScrollToShow(float top, float bottom, float viewHeight)
		{
			if (bottom <= viewHeight)
			{
				return 0.0f;
			}

			return std::max(0.0f, std::min(top, bottom - viewHeight));
		}

		// WARNING: Reckoned from the rows just drawn, not from the window's scrollbars, which still describe the entry
		// shown before. A sideways scrollbar is drawn over the view's last row, so the rows are kept above it.
		void ScrollTrackedRowsIntoView(const DetailLayout& layout)
		{
			if (!layout.TrackedTop.has_value())
			{
				return;
			}

			const ImVec2 size = ImGui::GetWindowSize();
			const float scrollbar = ImGui::GetStyle().ScrollbarSize;

			const bool scrollsDown = layout.Bottom > size.y;
			const bool scrollsSideways = layout.WidestRow > size.x - (scrollsDown ? scrollbar : 0.0f);
			const float viewHeight = size.y - (scrollsSideways ? scrollbar : 0.0f);

			const float scroll = ScrollToShow(*layout.TrackedTop, layout.TrackedBottom, viewHeight);

			if (scroll > 0.0f)
			{
				// The scroll is applied when the detail begins next frame, which the loop has to be asked for.
				ImGui::SetScrollY(scroll);
				Core::Redraw::Request();
			}
		}

		// What the detail shows, to tell when it shows another entry: the line, by the header naming it, the entry and
		// its commit.
		int DetailIdentity(const LinePeekContent& content, std::size_t shown)
		{
			const PeekEntry& entry = content.Entries[shown];
			const std::string key =
				std::format("{}#{}#{}", content.Header, shown, entry.Commit.has_value() ? entry.Commit->ToHex() : "");

			return static_cast<int>(ImGui::GetID(key.c_str()));
		}
	}

	std::optional<PeekAction> DrawLinePeek(const LinePeekContent& content, std::size_t selected, ImVec2 size,
										   const ResolvedScheme& scheme, bool justOpened)
	{
		const float codeSize = ImGui::GetFontSize();
		const PeekColours colours = ReadPeekColours(scheme);
		const std::size_t shown = selected < content.Entries.size() ? selected : 0;

		std::optional<PeekAction> answer;

		Core::PushInterfaceFont();

		const float top = ImGui::GetCursorPosY();

		if (DrawHeader(content, size.x))
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

		// WARNING: One child for every entry of every peek, set afresh whenever it shows another entry. ImGui keeps a
		// child's scroll by its id, and a child per entry reopened at the scroll the same-numbered entry of an earlier
		// peek was left at, with its title, byline and Show commit out of view.
		const bool ready = content.State == PeekState::Ready && shown < content.Entries.size();
		const int identity = ready ? DetailIdentity(content, shown) : kNothingShown;

		ImGuiStorage* const peekStorage = ImGui::GetStateStorage();
		const ImGuiID shownKey = ImGui::GetID("##LinePeekDetailShows");

		if (justOpened)
		{
			peekStorage->SetInt(shownKey, kNothingShown);
		}

		const bool entryJustShown = ready && peekStorage->GetInt(shownKey, kNothingShown) != identity;

		if (entryJustShown)
		{
			ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
		}

		if (ImGui::BeginChild("##LinePeekDiff", ImVec2(kFillRemainingWidth, bodyHeight), ImGuiChildFlags_None,
							  ImGuiWindowFlags_HorizontalScrollbar))
		{
			if (ready)
			{
				const DetailLayout layout = DrawEntryDetail(content.Entries[shown], codeSize, colours);

				if (layout.ShowCommit)
				{
					answer = PeekAction{.Action = PeekAction::Kind::ShowCommit, .Entry = shown};
				}

				if (entryJustShown)
				{
					ScrollTrackedRowsIntoView(layout);
					peekStorage->SetInt(shownKey, identity);
				}
			}
		}

		ImGui::EndChild();

		ImGui::PopFont();

		return answer;
	}

	bool PeekClosedBy(const PeekFrame& frame)
	{
		return frame.CaretLine != frame.PeekLine || frame.Edited || frame.CloseRequested;
	}
}
