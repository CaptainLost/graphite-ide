#include "Git/History/CommitListView.h"

#include "Core/Base/Narrow.h"

#include "Core/Base/Base.h"

#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/ImGui/Theme.h"
#include "Core/FileSystem/FileSystem.h"

#include "Git/History/CommitColumns.h"
#include "Git/History/CommitDetails.h"
#include "Git/History/CommitLabels.h"
#include "Git/History/CommitRows.h"
#include "UI/Lists/Chevron.h"
#include "UI/Lists/ListRow.h"
#include "UI/Lists/HighlightedRow.h"
#include "UI/FittedText.h"
#include "UI/TypographicMarks.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {

		constexpr float kLaneWidthPerLineHeight = 0.8f;
		constexpr float kDotRadiusPerLaneWidth = 0.22f;
		constexpr float kMinMessageDigitCount = 12.0f;
		constexpr float kHalf = 0.5f;
		constexpr float kNoOffset = 0.0f;
		constexpr float kNoSpacing = 0.0f;
		constexpr ImU32 kThemeTextColor = 0;
		constexpr std::string_view kNoHighlight = {};
		constexpr int kLeftMouseButton = 0;
		constexpr std::size_t kNoFileRows = 0;
		constexpr int kFrozenColumnCount = 0;
		constexpr int kFrozenRowCount = 0;

		// WARNING: CalcTextSize rounds each run up by less than a pixel, so a cell is given that pixel back. Without it a column sized to its text by a rescale cuts a hash it has room for.
		constexpr float kRoundedUpPixel = 1.0f;

		// WARNING: AllowOverlap keeps a click reaching the one Selectable under the row instead of being swallowed by the cells drawn over it.
		constexpr ImGuiSelectableFlags kRowSelectableFlags = ImGuiSelectableFlags_AllowOverlap;

		constexpr std::string_view kLocalChangesTitle = "Local changes";
		constexpr std::string_view kLocalChangesAge = "now";
		constexpr const char* kBroughtWaiting = "Reading the commits this merge brought\xE2\x80\xA6";
		constexpr const char* kBroughtFailed = "Cannot read the commits this merge brought: ";
		constexpr const char* kBroughtNone = "This merge brought no commits of its own";
		constexpr float kBroughtIndentPixels = 18.0f;
		constexpr float kElbowLeftPixels = 4.0f;
		constexpr float kElbowSizePixels = 8.0f;
		constexpr float kElbowBottomPixels = 1.0f;
		constexpr float kElbowAlpha = 0.5f;
		constexpr const char* kFoldTooltip = "Show them, dimmed, beside the branch";
		constexpr const char* kMiddleDot = " \xC2\xB7 ";
		constexpr float kKeepFontSize = 0.0f;
		constexpr float kUnderlineThickness = 1.0f;

		// The mockup's sizes, in its pixels at a 13 px font.
		constexpr float kMockupFontPixels = 13.0f;
		constexpr float kMessagePiecesGap = 6.0f;
		constexpr float kAuthorColumnPixels = 132.0f;
		constexpr float kAuthorAvatarOnlyColumnPixels = 20.0f;
		constexpr float kAvatarPixels = 16.0f;
		constexpr float kAvatarInitialsPixels = 8.5f;
		constexpr float kAvatarNameGapPixels = 6.0f;
		constexpr float kAgeColumnPixels = 38.0f;
		constexpr float kAgeTextPixels = 12.0f;
		constexpr float kHashColumnPixels = 64.0f;
		constexpr float kHashTextPixels = 11.5f;
		constexpr ImU32 kAvatarInitialsInk = IM_COL32(255, 255, 255, 255);
		constexpr float kRingRadius = 3.5f;    // to the middle of its stroke
		constexpr float kRingThickness = 2.0f;
		constexpr int kRingDashes = 6;

		float MockupPixels(float pixels)
		{
			return pixels * ImGui::GetFontSize() / kMockupFontPixels;
		}

		// The ring of work not yet committed, drawn in dashes.
		void DrawDashedRing(ImVec2 centre, ImU32 ink)
		{
			constexpr int kSegmentsPerDash = 4;
			const float dashAngle = std::numbers::pi_v<float> / static_cast<float>(kRingDashes);
			ImDrawList* drawList = ImGui::GetWindowDrawList();

			for (int dash = 0; dash < kRingDashes; ++dash)
			{
				const float from = dashAngle * static_cast<float>(dash * 2);

				drawList->PathArcTo(centre, MockupPixels(kRingRadius), from, from + dashAngle, kSegmentsPerDash);
				drawList->PathStroke(ink, ImDrawFlags_None, MockupPixels(kRingThickness));
			}
		}

		// The small elbow in front of a brought commit's text, tying it to its merge.
		void DrawBroughtElbow()
		{
			const ImVec2 cell = ImGui::GetCursorScreenPos();
			const float left = cell.x + MockupPixels(kElbowLeftPixels);
			const float bottom = cell.y + ImGui::GetTextLineHeight() * kHalf + MockupPixels(kElbowBottomPixels);
			const float top = bottom - MockupPixels(kElbowSizePixels);
			const ImU32 ink = Core::WithAlphaScaled(ImGui::GetColorU32(ImGuiCol_TextDisabled), kElbowAlpha);

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddLine(ImVec2(left, top), ImVec2(left, bottom), ink);
			drawList->AddLine(ImVec2(left, bottom), ImVec2(left + MockupPixels(kElbowSizePixels), bottom), ink);
		}

		GraphMetrics MetricsForCurrentFont()
		{
			GraphMetrics metrics;
			metrics.LaneWidth = ImGui::GetTextLineHeight() * kLaneWidthPerLineHeight;
			metrics.RowHeight = ListRowPitch();
			metrics.DotRadius = metrics.LaneWidth * kDotRadiusPerLaneWidth;
			return metrics;
		}

		enum class Column : int
		{
			Graph,
			Chevron,
			Message,
			Author,
			Age,
			Hash,
			Count
		};

		constexpr int TableIndexOf(Column column)
		{
			return static_cast<int>(column);
		}

		struct TextPiece
		{
			std::string_view Text;
			ImU32 Color;
		};

		// WARNING: ImGui runs a scrolling table's rows right up to its scrollbar with no style setting to leave room, so the work area is shortened by hand, along with the clip rects ImGui itself trims for an outer border.
		// The scrollbar's room is kept while it does not show as well: ImGui shows it a frame after the rows that need it,
		// and the columns, laid out from the right, would move in that frame.
		void KeepClearOfScrollbar(float gap)
		{
			ImGuiTable* table = ImGui::GetCurrentTable();

			if (table == nullptr)
			{
				return;
			}

			const float scrollbarRoom = table->InnerWindow->ScrollbarY ? 0.0f : ImGui::GetStyle().ScrollbarSize;
			const float right = std::max(table->WorkRect.Min.x, table->WorkRect.Max.x - gap - scrollbarRoom);

			table->WorkRect.Max.x = right;
			table->InnerClipRect.Max.x = std::min(table->InnerClipRect.Max.x, right);
			table->HostClipRect.Max.x = std::min(table->HostClipRect.Max.x, right);
		}

		float MinMessageWidth()
		{
			return ImGui::CalcTextSize("0").x * kMinMessageDigitCount;
		}

		// WARNING: Whether text came out is answered, because what follows it on the line is anchored to it. Empty text draws nothing and answers false. `keptFree` is left at the cell's end for what is drawn after the text.
		bool DrawFittedCell(std::string_view text, ImU32 color, std::string_view needle, float keptFree = 0.0f)
		{
			if (text.empty())
			{
				return false;
			}

			const float roomInCell = ImGui::GetContentRegionAvail().x + kRoundedUpPixel - keptFree;
			const float ellipsisWidth = Core::TextWidth(kEllipsis);

			const std::optional<std::size_t> cut = CutToFit(text, roomInCell, ellipsisWidth, Core::TextWidth);

			// WARNING: Nothing rather than an ellipsis the cell would clip, as FitToWidth does.
			if (cut.has_value() && ellipsisWidth > roomInCell)
			{
				return false;
			}

			const HighlightedLabel split =
				SplitHighlight(cut.has_value() ? text.substr(0, *cut) : text, needle);

			const ImU32 ink = color != kThemeTextColor ? color : ImGui::GetColorU32(ImGuiCol_Text);
			const ImU32 mark = ImGui::GetColorU32(ImGuiCol_CheckMark);

			const TextPiece pieces[] = {
				{split.Before, ink},
				{split.Match, mark},
				{split.After, ink},
				{cut.has_value() ? kEllipsis : std::string_view{}, ink},
			};

			bool isFirstPiece = true;

			for (const TextPiece& piece : pieces)
			{
				if (piece.Text.empty())
				{
					continue;
				}

				if (!isFirstPiece)
				{
					ImGui::SameLine(kNoOffset, kNoSpacing);
				}

				isFirstPiece = false;

				ImGui::PushStyleColor(ImGuiCol_Text, piece.Color);
				ImGui::TextUnformatted(piece.Text.data(), piece.Text.data() + piece.Text.size());
				ImGui::PopStyleColor();
			}

			return true;
		}

		// A cell drawn in a font smaller than the row's stands in the middle of the row's line, as the mockup's centred flex items do.
		void CentreSmallerFontInLine(float lineHeight)
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::floor((lineHeight - ImGui::GetTextLineHeight()) * kHalf));
		}

		// Right-aligned in its cell at the age's size.
		void DrawAge(const std::string& age, ImU32 ink)
		{
			const float lineHeight = ImGui::GetTextLineHeight();

			ImGui::PushFont(nullptr, MockupPixels(kAgeTextPixels));
			CentreSmallerFontInLine(lineHeight);

			const float room = ImGui::GetContentRegionAvail().x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, room - ImGui::CalcTextSize(age.c_str()).x));

			ImGui::PushStyleColor(ImGuiCol_Text, ink);
			ImGui::TextUnformatted(age.c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}

		// The round avatar with the author's initials, a text line high so it does not move the row.
		void DrawAvatar(const Core::Signature& author, float alpha)
		{
			const float lineHeight = ImGui::GetTextLineHeight();
			const float size = MockupPixels(kAvatarPixels);
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const ImVec2 centre(origin.x + size * kHalf, origin.y + lineHeight * kHalf);
			const std::string initials = AuthorInitials(author);

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddCircleFilled(centre, size * kHalf, Core::WithAlphaScaled(AvatarFill(AvatarHue(author.Email)), alpha));

			ImGui::PushFont(Core::ImGuiRenderer::GetBoldFont(), MockupPixels(kAvatarInitialsPixels));
			const ImVec2 textSize = ImGui::CalcTextSize(initials.c_str());
			drawList->AddText(ImVec2(std::floor(centre.x - textSize.x * kHalf), std::floor(centre.y - textSize.y * kHalf)),
							  Core::WithAlphaScaled(kAvatarInitialsInk, alpha), initials.c_str());
			ImGui::PopFont();

			ImGui::Dummy(ImVec2(size, lineHeight));
		}

		void RefLabels(const std::vector<CommitRef>& refs, const GitPalette& palette,
					   std::string_view summary, float alpha)
		{
			if (refs.empty())
			{
				return;
			}

			const float labelGap = MockupPixels(kMessagePiecesGap);

			std::vector<RefPill> pills;
			std::vector<float> labelWidths;
			pills.reserve(refs.size());
			labelWidths.reserve(refs.size());

			for (const CommitRef& ref : refs)
			{
				pills.push_back(MeasureRefPill(ref));
				labelWidths.push_back(pills.back().Width);
			}

			const std::size_t fittingCount = RefLabelsThatFit(
				labelWidths, labelGap, ImGui::GetContentRegionAvail().x + kRoundedUpPixel,
				Core::TextWidth(summary), MinMessageWidth());

			for (std::size_t labelIndex = 0; labelIndex < fittingCount; ++labelIndex)
			{
				DrawRefPill(pills[labelIndex], palette, alpha);
				ImGui::SameLine(kNoOffset, labelGap);
			}
		}

		std::string FormattedSignature(const Core::Signature& person)
		{
			return std::format("{} <{}>", person.Name, person.Email);
		}

		std::string FormattedMoment(std::chrono::sys_seconds when, std::chrono::sys_seconds now,
									const std::chrono::time_zone* zone)
		{
			return std::format("{}  ·  {}", FormattedTimestamp(when, zone),
							   RelativeAge(when, now));
		}

		void CellTooltip(int column, const Core::CommitSummary& commit,
						 std::chrono::sys_seconds now, const std::chrono::time_zone* zone)
		{
			const bool wasCommittedAtAnotherTime = commit.Committer.When != commit.Author.When;

			std::string text;

			switch (static_cast<Column>(column))
			{
			case Column::Hash:
				text = commit.Id.ToHex();
				break;

			case Column::Author:
				text = FormattedSignature(commit.Author);

				if (ShowsCommitter(commit))
				{
					text += "\ncommitted by " + FormattedSignature(commit.Committer);
				}

				break;

			case Column::Age:
				text = FormattedMoment(commit.Author.When, now, zone);

				if (wasCommittedAtAnotherTime)
				{
					text += "\ncommitted " + FormattedMoment(commit.Committer.When, now, zone);
				}

				break;

			default:
				return;
			}

			ImGui::SetTooltip("%s", text.c_str());
		}

		std::optional<int> DisplayRowOf(const std::optional<Core::Oid>& commit, const Core::LogPage& page,
										const CommitRows& rows)
		{
			if (!commit.has_value())
			{
				return std::nullopt;
			}

			for (std::size_t commitIndex = 0; commitIndex < page.Commits.size(); ++commitIndex)
			{
				if (page.Commits[commitIndex].Id == *commit)
				{
					return Core::Narrow<int>(rows.RowOfCommit[commitIndex]);
				}
			}

			return std::nullopt;
		}

		// File History names a file at the root "/README.md" so that it names that file alone, and its row reads
		// "README.md".
		std::string_view WithoutRootAnchor(std::string_view pathHighlight)
		{
			return pathHighlight.starts_with('/') ? pathHighlight.substr(1) : pathHighlight;
		}
	}

	std::optional<FileSelection> SelectionSurviving(const std::optional<FileSelection>& selection,
													const Core::LogPage& page)
	{
		if (!selection.has_value())
		{
			return std::nullopt;
		}

		for (const Core::CommitSummary& commit : page.Commits)
		{
			if (commit.Id == selection->Commit)
			{
				return selection;
			}
		}

		return std::nullopt;
	}

	std::optional<Core::Oid> CommitSelectionSurviving(const std::optional<Core::Oid>& commit,
													  const Core::LogPage& page)
	{
		if (!commit.has_value())
		{
			return std::nullopt;
		}

		for (const Core::CommitSummary& listed : page.Commits)
		{
			if (listed.Id == *commit)
			{
				return commit;
			}
		}

		return std::nullopt;
	}

	Core::Ref<const Core::LogPage> BroughtCountedFor(const HistoryMarks& marks, const Core::CommitSummary& commit)
	{
		if (!marks.BroughtOf || !IsMerge(commit))
		{
			return nullptr;
		}

		return marks.BroughtOf(commit.Id);
	}

	void CommitListView::KeepSelectionWithin(const Core::LogPage& page, const HistoryMarks& marks)
	{
		m_SelectedFile = SelectionSurviving(m_SelectedFile, page);

		const std::optional<Core::Oid> selected = m_SelectedCommit;
		m_SelectedCommit = CommitSelectionSurviving(selected, page);

		// A commit a merge brought is kept by that merge, as long as it is in the page and open.
		if (selected.has_value() && !m_SelectedCommit.has_value() && ListedUnderOpenMerge(*selected, page, marks))
		{
			m_SelectedCommit = selected;
		}
	}

	bool CommitListView::ListedUnderOpenMerge(const Core::Oid& commit, const Core::LogPage& page,
											  const HistoryMarks& marks) const
	{
		if (!marks.BroughtOf)
		{
			return false;
		}

		for (const Core::CommitSummary& merge : page.Commits)
		{
			if (!IsMerge(merge) || !IsExpanded(merge.Id))
			{
				continue;
			}

			const Core::Ref<const Core::LogPage> brought = marks.BroughtOf(merge.Id);

			if (brought && CommitSelectionSurviving(commit, *brought).has_value())
			{
				return true;
			}
		}

		return false;
	}

	bool CommitListView::IsExpanded(const Core::Oid& commit) const
	{
		return m_Expanded.contains(commit);
	}

	void CommitListView::ToggleExpanded(const Core::Oid& commit)
	{
		const std::size_t erasedCount = m_Expanded.erase(commit);

		if (erasedCount > 0)
		{
			const bool collapsedCommitHoldsSelectedFile =
				m_SelectedFile.has_value() && m_SelectedFile->Commit == commit;

			if (collapsedCommitHoldsSelectedFile)
			{
				m_SelectedFile.reset();
			}
		}
		else
		{
			m_Expanded.insert(commit);
		}
	}

	void CommitListView::SetExpanded(const Core::Oid& commit, bool expanded)
	{
		if (IsExpanded(commit) != expanded)
		{
			ToggleExpanded(commit);
		}
	}

	void CommitListView::SelectCommit(const Core::Oid& commit)
	{
		m_SelectedCommit = commit;
		m_SelectedFile.reset();
		m_LocalChangesSelected = false;
	}

	void CommitListView::SelectFile(const FileSelection& file)
	{
		m_SelectedFile = file;
		m_LocalChangesSelected = false;
	}

	void CommitListView::SelectLocalChanges()
	{
		m_LocalChangesSelected = true;
		m_SelectedCommit.reset();
		m_SelectedFile.reset();
	}

	void CommitListView::ClearRowSelection()
	{
		m_SelectedCommit.reset();
		m_LocalChangesSelected = false;
	}

	void CommitListView::Reset()
	{
		m_Expanded.clear();
		m_SelectedFile.reset();
		m_SelectedCommit.reset();
		m_LocalChangesSelected = false;
		m_ScrollTarget.reset();
		m_ScrollToTop = false;
		m_FoldClicked = false;
		m_LocalChangesClicked = false;
	}

	bool CommitListView::TakeFoldClick()
	{
		return std::exchange(m_FoldClicked, false);
	}

	bool CommitListView::TakeLocalChangesClick()
	{
		return std::exchange(m_LocalChangesClicked, false);
	}

	std::optional<CommitAction> CommitListView::TakeCommitAction()
	{
		std::optional<CommitAction> action = std::move(m_CommitAction);
		m_CommitAction.reset();
		return action;
	}

	std::size_t CommitListView::FileRowCountUnder(
		const Core::LogPage& page, std::size_t commitIndex,
		const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf) const
	{
		const Core::CommitSummary& summary = page.Commits[commitIndex];

		if (!IsExpanded(summary.Id))
		{
			return kNoFileRows;
		}

		const Core::Ref<const Core::Diff> diff = diffOf(summary.Id);

		return FileRowsOfExpanded(diff ? diff->Files.size() : kNoFileRows);
	}

	void CommitListView::Render(const Core::LogPage& page, const Core::CommitGraph& graph,
								const GitPalette& palette, const std::chrono::time_zone* zone,
								const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf,
								const std::function<bool(const Core::Oid&)>& diffFailed,
								const CommitRefMap& refs, std::string_view highlight,
								std::string_view pathHighlight,
								const HistoryMarks& marks)
	{
		// WARNING: A Local changes row no longer drawn takes its selection with it, or the preview would go on showing
		// the changes under a history that has no row for them.
		if (!marks.LocalChanges.has_value())
		{
			m_LocalChangesSelected = false;
		}

		// A wait is one row of the list; only a list with nothing in it and nothing on its way gives way to the words.
		const bool hasNothingToList =
			page.Commits.empty() && !marks.LocalChanges.has_value() && !marks.Waiting.has_value();

		if (hasNothingToList)
		{
			m_ScrollTarget.reset();
			m_ScrollToTop = false;

			constexpr float kSmallestRegion = 1.0f;
			const float height = std::max(ImGui::GetContentRegionAvail().y - m_RoomBelow, kSmallestRegion);

			Core::EmptyState(m_EmptyText, nullptr, ImVec2(0.0f, height));
			return;
		}

		const GraphMetrics metrics = MetricsForCurrentFont();

		m_HoveredCommit.reset();

		const bool hasFold = marks.Fold.has_value() && marks.Fold->BeforeCommit < page.Commits.size();
		const ExtraRows extra{.LocalChanges = marks.LocalChanges.has_value(),
							  .Banner = marks.Banner.has_value(),
							  .FoldBeforeCommit = hasFold ? std::optional<std::size_t>{marks.Fold->BeforeCommit} : std::nullopt,
							  .Waiting = marks.Waiting.has_value()};

		// In the Merges history an opened merge lists what it brought where another commit lists its files.
		const auto opensBrought = [&](std::size_t commitIndex) {
			return marks.BroughtOf && IsMerge(page.Commits[commitIndex]) && IsExpanded(page.Commits[commitIndex].Id);
		};

		ExtraRows extraWithBrought = extra;
		extraWithBrought.BroughtRowsUnder = [&](std::size_t commitIndex) -> std::size_t {
			if (!opensBrought(commitIndex))
			{
				return kNoFileRows;
			}

			return BroughtRowsOf(marks.BroughtOf(page.Commits[commitIndex].Id).get());
		};

		const CommitRows rows = BuildCommitRows(
			page.Commits.size(),
			[&](std::size_t commitIndex) {
				return opensBrought(commitIndex) ? kNoFileRows : FileRowCountUnder(page, commitIndex, diffOf);
			},
			extraWithBrought);

		// The ring of the Local changes row takes a lane even while no commit has one.
		constexpr std::size_t kLocalChangesLanes = 1;
		const std::size_t laneCount =
			marks.LocalChanges.has_value() ? std::max(graph.LaneCount, kLocalChangesLanes) : graph.LaneCount;

		const std::chrono::sys_seconds now =
			std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

		// WARNING: A table row is as tall as its tallest cell plus the cell padding, so the padding is half the list row spacing on each side: one line of text plus both halves is RowHeight, the list row pitch, which the clipper seeks by and the graph is placed by. The minimum height passed to each row says the same again, so a shorter cell cannot pull the rows up away from the graph.
		const float cellPaddingY = ListTableCellPadding().y;

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ListTableCellPadding());

		// The columns have the mockup's widths and no header row, so none of them is resized by the reader.
		constexpr ImGuiTableFlags flags = ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;

		const ImGuiStyle& style = ImGui::GetStyle();

		const float widthBesideScrollbar = ImGui::GetContentRegionAvail().x - style.ScrollbarSize;

		if (ImGui::BeginTable("##commits", TableIndexOf(Column::Count), flags, ImVec2(0.0f, -m_RoomBelow)))
		{
			PadListTableEnds();
			KeepClearOfScrollbar(ImGui::GetStyle().ItemSpacing.x);

			CommitColumnsInput columnInput;
			columnInput.Available = widthBesideScrollbar;
			columnInput.Graph = GraphWidth(metrics, laneCount);
			columnInput.FontSize = ImGui::GetFontSize();

			const CommitColumns columns = LayoutCommitColumns(columnInput);
			m_ShowsAuthorName = columns.ShowAuthorName;

			const float chevronWidth = std::max(ImGui::CalcTextSize(ChevronGlyph(false)).x, ImGui::CalcTextSize(ChevronGlyph(true)).x);
			const float authorWidth = MockupPixels(columns.ShowAuthorName ? kAuthorColumnPixels : kAuthorAvatarOnlyColumnPixels);
			const float ageWidth = MockupPixels(kAgeColumnPixels);
			const float hashWidth = MockupPixels(kHashColumnPixels);

			constexpr ImGuiTableColumnFlags kFixedWidth = ImGuiTableColumnFlags_WidthFixed;
			constexpr ImGuiTableColumnFlags kFixedAndLocked = kFixedWidth | ImGuiTableColumnFlags_NoResize;

			// WARNING: A narrow table gives up the hash with the Disabled flag, which is read again every frame. The table is not Hideable, so ImGui offers no menu to bring it back and does not save the state.
			const ImGuiTableColumnFlags hashFlags =
				columns.ShowHash ? kFixedAndLocked : kFixedAndLocked | ImGuiTableColumnFlags_Disabled;

			// A fixed column that cannot be resized has the width given here applied again every frame.
			ImGui::TableSetupColumn("##graph", kFixedAndLocked, columns.Graph);
			ImGui::TableSetupColumn("##chevron", kFixedAndLocked, chevronWidth);
			ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Author", kFixedAndLocked, authorWidth);
			ImGui::TableSetupColumn("Age", kFixedAndLocked, ageWidth);
			ImGui::TableSetupColumn("Hash", hashFlags, hashWidth);

			ImGui::TableSetupScrollFreeze(kFrozenColumnCount, kFrozenRowCount);

			if (std::exchange(m_ScrollToTop, false))
			{
				ImGui::SetScrollY(0.0f);
			}

			const std::optional<int> scrollRow = DisplayRowOf(std::exchange(m_ScrollTarget, std::nullopt), page, rows);

			ImGuiListClipper clipper;
			clipper.Begin(Core::Narrow<int>(rows.Rows.size()), metrics.RowHeight);

			// WARNING: The clipper only draws the rows in view, and SetScrollHereY reads the row just
			// drawn, so a row scrolled far away is made to draw this once.
			if (scrollRow.has_value())
			{
				clipper.IncludeItemByIndex(*scrollRow);
			}

			while (clipper.Step())
			{
				// WARNING: Display row 0 is usually scrolled far above the table and nothing is drawn at it, so its position is worked back from a row drawn in this step.
				ImVec2 graphOrigin;

				for (int displayRow = clipper.DisplayStart; displayRow < clipper.DisplayEnd;
					 ++displayRow)
				{
					const CommitRow& row = rows.Rows[static_cast<std::size_t>(displayRow)];

					ImGui::TableNextRow(ImGuiTableRowFlags_None, metrics.RowHeight);
					ImGui::PushID(displayRow);

					ImGui::TableSetColumnIndex(TableIndexOf(Column::Graph));
					const ImVec2 graphCell = ImGui::GetCursorScreenPos();
					graphOrigin = ImVec2(graphCell.x, graphCell.y - cellPaddingY -
														  static_cast<float>(displayRow) * metrics.RowHeight);

					switch (row.Kind)
					{
					case CommitRowKind::LocalChanges:
					{
						constexpr std::size_t kFirstLane = 0;
						const ImVec2 dotCentre(graphOrigin.x + LaneCentreX(metrics, kFirstLane),
											   graphOrigin.y + RowCentreY(metrics, static_cast<std::size_t>(displayRow)));

						RenderLocalChangesRow(*marks.LocalChanges, palette, dotCentre);
						break;
					}
					case CommitRowKind::Fold:
						RenderFoldRow(*marks.Fold);
						break;
					case CommitRowKind::Note:
						RenderNoteRow(marks.Waiting);
						break;
					case CommitRowKind::Banner:
						RenderBannerRow(*marks.Banner);
						break;
					case CommitRowKind::Commit:
					{
						const Core::CommitSummary& commit = page.Commits[row.Commit];
						const bool dimmed = marks.Dimmed != nullptr && marks.Dimmed->contains(commit.Id);

						const Core::Ref<const Core::LogPage> brought = BroughtCountedFor(marks, commit);

						RenderCommitRow(page, commit, palette, refs, highlight, now, zone, dimmed,
										BroughtDrawing{.Page = brought.get(),
													   .IsBrought = false,
													   .OpensBrought = marks.BroughtOf && IsMerge(commit)});

						if (scrollRow == displayRow)
						{
							ImGui::SetScrollHereY(kHalf);
						}

						break;
					}
					case CommitRowKind::Brought:
					{
						const Core::CommitSummary& merge = page.Commits[row.Commit];
						const Core::Ref<const Core::LogPage> brought = marks.BroughtOf(merge.Id);

						// Scoped by the merge, not the row: opening a merge above moves this row to another index.
						const char* const idBytes = reinterpret_cast<const char*>(merge.Id.Bytes.data());
						ImGui::PushID(idBytes, idBytes + merge.Id.Bytes.size());

						if (brought && row.File < brought->Commits.size())
						{
							RenderCommitRow(*brought, brought->Commits[row.File], palette, refs, highlight, now, zone,
											false, BroughtDrawing{.Page = nullptr, .IsBrought = true});
						}
						else
						{
							RenderBroughtNote(brought.get(), brought || !marks.BroughtFailure ? std::nullopt
																								 : marks.BroughtFailure(merge.Id));
						}

						ImGui::PopID();
						break;
					}
					case CommitRowKind::File:
						RenderFileRow(page.Commits[row.Commit], row.File, palette, diffOf, diffFailed,
									  WithoutRootAnchor(pathHighlight));
						break;
					}

					ImGui::PopID();
				}

				// WARNING: The lanes are drawn once per step from inside the graph column of the step's last row, still open because the next row has not begun, so they land in that column's clip rect, which stops below the header row.
				if (clipper.DisplayEnd > clipper.DisplayStart)
				{
					ImGui::TableSetColumnIndex(TableIndexOf(Column::Graph));

					// A dimmed commit is drawn with its row: its dot and the lanes that end at it.
					CommitGraphView::AlphaOfCommit alphaOfCommit;

					if (marks.Dimmed != nullptr)
					{
						alphaOfCommit = [&](std::size_t commitIndex) {
							const Core::Oid& commit = page.Commits[commitIndex].Id;

							if (!marks.Dimmed->contains(commit))
							{
								return 1.0f;
							}

							return IsPointedAt(commit) ? kDimmedRowPointedAlpha : kDimmedRowAlpha;
						};
					}

					CommitGraphView{}.Render(graph, palette, metrics, rows.RowOfCommit,
											 clipper.DisplayStart, clipper.DisplayEnd - 1,
											 graphOrigin, alphaOfCommit);
				}
			}

			ImGui::EndTable();
		}

		ImGui::PopStyleVar();
	}

	void CommitListView::RenderCommitRow(const Core::LogPage& page, const Core::CommitSummary& commit,
										 const GitPalette& palette, const CommitRefMap& refs,
										 std::string_view highlight, std::chrono::sys_seconds now,
										 const std::chrono::time_zone* zone, bool dimmed,
										 const BroughtDrawing& brought)
	{
		const bool expanded = IsExpanded(commit.Id);
		const bool isSelectedCommit = m_SelectedCommit == commit.Id;

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Chevron));

		const float chevronZoneStartX =
			ImGui::GetCursorScreenPos().x - ImGui::GetStyle().CellPadding.x;

		const ImVec2 rowStart = ImGui::GetCursorPos();

		const bool selectableClicked = TableRowSelectable("##commit", isSelectedCommit, kRowSelectableFlags);

		// WARNING: Hover and the context menu are asked of the Selectable here, because every cell below becomes the last item in turn. The caller pushes an id per row, so each row's menu is its own popup.
		const bool selectableHovered = ImGui::IsItemHovered();
		const bool selectableHoveredForTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);

		if (selectableHovered)
		{
			m_HoveredCommit = commit.Id;
		}

		if (Core::BeginRightClickPopup("commit-menu"))
		{
			const bool writesAllowed = m_ActionsEnabled;

			ImGui::BeginDisabled(!writesAllowed);

			if (ImGui::MenuItem("Checkout (detached)"))
			{
				m_CommitAction = CommitAction{.Action = CommitAction::Kind::Checkout, .Commit = commit.Id};
			}

			if (ImGui::MenuItem("New Branch from Here…"))
			{
				m_CommitAction = CommitAction{.Action = CommitAction::Kind::NewBranch, .Commit = commit.Id};
			}

			ImGui::EndDisabled();

			ImGui::Separator();

			if (ImGui::MenuItem("Copy Hash"))
			{
				m_CommitAction = CommitAction{.Action = CommitAction::Kind::Copy,
											  .Commit = commit.Id,
											  .Text = commit.Id.ToHex(),
											  .What = "commit hash"};
			}

			if (ImGui::MenuItem("Copy Short Hash"))
			{
				m_CommitAction = CommitAction{.Action = CommitAction::Kind::Copy,
											  .Commit = commit.Id,
											  .Text = commit.Id.ShortHex(),
											  .What = "short hash"};
			}

			if (ImGui::MenuItem("Copy Message"))
			{
				m_CommitAction = CommitAction{.Action = CommitAction::Kind::Copy,
											  .Commit = commit.Id,
											  .Text = CommitMessageText(commit),
											  .What = "commit message"};
			}

			ImGui::EndPopup();
		}

		ImGui::SetCursorPos(rowStart);

		// A dimmed row is dimmed whole, and less while it is pointed at or selected.
		const float alpha =
			!dimmed ? 1.0f
					: (isSelectedCommit || selectableHovered ? CommitListView::kDimmedRowPointedAlpha
															 : CommitListView::kDimmedRowAlpha);

		// A commit a merge brought does not open files. The room of the chevron is kept on every row, drawn or not, so the
		// message never moves; a merge of the Merges history shows it always, the rest while pointed at, selected or open.
		const bool showsChevron = !brought.IsBrought && (brought.OpensBrought || selectableHovered || isSelectedCommit || expanded);

		if (showsChevron)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
			DrawChevron(expanded);
			ImGui::PopStyleVar();
		}

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));
		const float chevronZoneEndX = ImGui::GetCursorScreenPos().x - ImGui::GetStyle().CellPadding.x;

		const ImU32 dimmedInk = Core::WithAlphaScaled(ImGui::GetColorU32(ImGuiCol_TextDisabled), alpha);
		const ImU32 hashInk = page.Unpushed.contains(commit.Id) ? Core::WithAlphaScaled(palette.GutterAdded, alpha) : dimmedInk;
		const ImU32 summaryInk = dimmed ? Core::WithAlphaScaled(ImGui::GetColorU32(ImGuiCol_Text), alpha) : kThemeTextColor;

		if (brought.IsBrought)
		{
			DrawBroughtElbow();
			ImGui::Indent(MockupPixels(kBroughtIndentPixels));
		}

		if (refs.contains(commit.Id))
		{
			RefLabels(refs.at(commit.Id), palette, commit.Summary, alpha);
		}

		// An opened merge says how many commits it brought after its own subject, dim as the mockup's count.
		const std::string count = brought.Page != nullptr ? kMiddleDot + DescribeBrought(*brought.Page) : std::string{};

		const bool drewSubject = DrawFittedCell(commit.Summary, summaryInk, highlight, Core::TextWidth(count));

		// WARNING: The count is anchored to the subject, so a subject that did not come out leaves it undrawn too, or it would stand beside whatever came before.
		if (!count.empty() && (drewSubject || commit.Summary.empty()))
		{
			if (drewSubject)
			{
				ImGui::SameLine(kNoOffset, kNoSpacing);
			}

			DrawFittedCell(count, dimmedInk, kNoHighlight);
		}

		if (brought.IsBrought)
		{
			ImGui::Unindent(MockupPixels(kBroughtIndentPixels));
		}

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Author));
		DrawAvatar(commit.Author, alpha);

		if (m_ShowsAuthorName)
		{
			ImGui::SameLine(kNoOffset, MockupPixels(kAvatarNameGapPixels));
			DrawFittedCell(AuthorName(commit.Author), dimmedInk, highlight);
		}

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Age));
		DrawAge(ShortAge(commit.Author.When, now), dimmedInk);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Hash));

		{
			const float lineHeight = ImGui::GetTextLineHeight();

			Core::PushCodeFont(MockupPixels(kHashTextPixels));
			CentreSmallerFontInLine(lineHeight);
			DrawFittedCell(commit.Id.ShortHex(), hashInk, highlight);
			ImGui::PopFont();
		}

		if (selectableHoveredForTooltip)
		{
			CellTooltip(ImGui::TableGetHoveredColumn(), commit, now, zone);
		}

		if (!selectableClicked)
		{
			return;
		}

		if (brought.IsBrought)
		{
			SelectCommit(commit.Id);
			return;
		}

		// WARNING: A Selectable fires on release, so where the press landed decides, not where the pointer is now. Hover is checked too, so an activation that did not come from the mouse, which leaves MouseClickedPos stale, falls to selecting.
		const float pressedX = ImGui::GetIO().MouseClickedPos[kLeftMouseButton].x;
		const bool onChevron =
			selectableHovered && pressedX >= chevronZoneStartX && pressedX < chevronZoneEndX;

		if (onChevron)
		{
			ToggleExpanded(commit.Id);
		}
		else
		{
			SelectCommit(commit.Id);
		}
	}

	void CommitListView::RenderBroughtNote(const Core::LogPage* brought, const std::optional<std::string>& failure)
	{
		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));
		ImGui::Indent(MockupPixels(kBroughtIndentPixels));

		if (brought == nullptr && !failure.has_value())
		{
			Core::LoadingLine(kBroughtWaiting);
		}
		else
		{
			const std::string text = failure.has_value() ? kBroughtFailed + *failure : std::string{kBroughtNone};

			DrawFittedCell(text, ImGui::GetColorU32(ImGuiCol_TextDisabled), kNoHighlight);
		}

		ImGui::Unindent(MockupPixels(kBroughtIndentPixels));
	}

	void CommitListView::RenderLocalChangesRow(const std::string& summary, const GitPalette& palette, ImVec2 dotCentre)
	{
		DrawDashedRing(dotCentre, m_LocalChangesInk != 0 ? m_LocalChangesInk : palette.GutterModified);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Chevron));

		if (TableRowSelectable("##localChanges", m_LocalChangesSelected, kRowSelectableFlags))
		{
			SelectLocalChanges();
			m_LocalChangesClicked = true;
		}

		const ImU32 dimmedInk = ImGui::GetColorU32(ImGuiCol_TextDisabled);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));
		DrawFittedCell(kLocalChangesTitle, Core::EmphasisText(), kNoHighlight);
		ImGui::SameLine(kNoOffset, MockupPixels(kMessagePiecesGap));
		DrawFittedCell(summary, dimmedInk, kNoHighlight);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Age));
		DrawAge(std::string{kLocalChangesAge}, dimmedInk);
	}

	void CommitListView::RenderFoldRow(const FoldRow& fold)
	{
		ImGui::TableSetColumnIndex(TableIndexOf(Column::Chevron));

		constexpr bool kNeverSelected = false;

		if (TableRowSelectable("##fold", kNeverSelected, kRowSelectableFlags))
		{
			m_FoldClicked = true;
		}

		const bool hovered = ImGui::IsItemHovered();
		ImGui::SetItemTooltip("%s", kFoldTooltip);

		const ImU32 dimmedInk = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const ImU32 actionInk = ImGui::GetColorU32(ImGuiCol_CheckMark);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));

		// WARNING: The italic face is not loaded where no renderer is, and a null face keeps the current one.
		// The count comes with the row (its rows wait for it), so the action follows the text as in the mockup.
		ImGui::PushFont(Core::ImGuiRenderer::GetItalicFont(), kKeepFontSize);
		DrawFittedCell(fold.Text, dimmedInk, kNoHighlight);
		ImGui::PopFont();

		ImGui::SameLine(kNoOffset, MockupPixels(kMessagePiecesGap));

		const float actionLeft = ImGui::GetCursorScreenPos().x;
		DrawFittedCell(fold.Action, actionInk, kNoHighlight);

		if (hovered && ImGui::GetItemRectMax().x > actionLeft)
		{
			const float underline = std::floor(ImGui::GetItemRectMin().y + ImGui::GetFontSize());
			ImGui::GetWindowDrawList()->AddLine(ImVec2(actionLeft, underline), ImVec2(ImGui::GetItemRectMax().x, underline),
												actionInk, kUnderlineThickness);
		}
	}

	void CommitListView::RenderBannerRow(const std::string& banner)
	{
		// The row is tinted as the banner above the list was, and says the sentence in the message column.
		ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_FrameBg));
		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));
		DrawFittedCell(banner, ImGui::GetColorU32(ImGuiCol_Text), kNoHighlight);
		ImGui::SetItemTooltip("%s", banner.c_str());
	}

	void CommitListView::RenderNoteRow(const std::optional<std::string>& waiting)
	{
		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));

		if (waiting.has_value())
		{
			Core::LoadingLine(waiting->c_str());
			return;
		}

		DrawFittedCell(m_EmptyText, ImGui::GetColorU32(ImGuiCol_TextDisabled), kNoHighlight);

		// WARNING: An empty text draws nothing, and a row must end on an item.
		if (m_EmptyText.empty())
		{
			ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
		}
	}

	void CommitListView::RenderFileRow(
		const Core::CommitSummary& commit, std::size_t fileIndex, const GitPalette& palette,
		const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf,
		const std::function<bool(const Core::Oid&)>& diffFailed, std::string_view pathHighlight)
	{
		const Core::Ref<const Core::Diff> diff = diffOf(commit.Id);

		const Core::FileDiff* changedFile =
			(diff && fileIndex < diff->Files.size()) ? &diff->Files[fileIndex] : nullptr;

		if (changedFile == nullptr)
		{
			// WARNING: A placeholder is still a row, drawn disabled, so the graph stays on the rows it labels. The diff may be loading, failed, empty, or evicted from the cache between building the rows and drawing them.
			ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));

			if (!diff && diffFailed(commit.Id))
			{
				ImGui::TextDisabled("Could not load changes");
			}
			else if (!diff)
			{
				// WARNING: Scoped by the commit, not the row: expanding a commit above moves this row to
				// another index, and the wait would start over or pass to another row's loader.
				const char* const idBytes = reinterpret_cast<const char*>(commit.Id.Bytes.data());
				ImGui::PushID(idBytes, idBytes + commit.Id.Bytes.size());
				Core::LoadingLine("Loading files");
				ImGui::PopID();
			}
			else if (diff->Files.empty())
			{
				ImGui::TextDisabled("No file changes");
			}
			else
			{
				ImGui::TextDisabled("%s", std::string(kEllipsis).c_str());
			}

			return;
		}

		const std::filesystem::path& path = ShownPath(*changedFile);

		const bool isSelectedFile = m_SelectedFile.has_value() &&
									m_SelectedFile->Commit == commit.Id &&
									m_SelectedFile->Path == path;

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Chevron));

		if (TableRowSelectable("##file", isSelectedFile, kRowSelectableFlags))
		{
			SelectFile(FileSelection{commit.Id, path});
		}

		if (Core::BeginRightClickPopup("file-menu"))
		{
			if (ImGui::MenuItem("Copy Path"))
			{
				m_CommitAction = CommitAction{.Action = CommitAction::Kind::Copy,
											  .Commit = commit.Id,
											  .Text = Core::ToUtf8(path),
											  .What = "path"};
			}

			if (ImGui::MenuItem("Copy File Name"))
			{
				m_CommitAction = CommitAction{.Action = CommitAction::Kind::Copy,
											  .Commit = commit.Id,
											  .Text = Core::ToUtf8(path.filename()),
											  .What = "file name"};
			}

			ImGui::EndPopup();
		}

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));

		const std::string letter(1, ChangeLetter(changedFile->Change));

		// WARNING: Asked before drawing, because in a column too narrow for it the letter gives way to an ellipsis, which the change name would not explain.
		const float letterWidth = ImGui::CalcTextSize(letter.c_str()).x;
		const float roomForLetter = ImGui::GetContentRegionAvail().x + kRoundedUpPixel;
		const bool letterFits = letterWidth <= roomForLetter;

		DrawFittedCell(letter, ChangeColor(changedFile->Change, palette), kNoHighlight);

		if (const char* changeName = ChangeName(changedFile->Change);
			changeName != nullptr && letterFits)
		{
			ImGui::SetItemTooltip("%s", changeName);
		}

		ImGui::SameLine(kNoOffset, kNoSpacing);
		DrawFittedCell(std::format("  {}", Core::ToUtf8(path)), kThemeTextColor, pathHighlight);
	}
}
