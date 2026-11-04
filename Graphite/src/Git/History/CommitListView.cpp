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
		constexpr ImU32 kLabelFillAlphaByte = 0x38u;
		constexpr int kLeftMouseButton = 0;
		constexpr std::size_t kNoFileRows = 0;
		constexpr int kFrozenColumnCount = 0;
		constexpr int kFrozenRowCount = 1;

		// WARNING: CalcTextSize rounds each run up by less than a pixel, so a cell is given that pixel back. Without it a column sized to its text by a rescale cuts a hash it has room for.
		constexpr float kRoundedUpPixel = 1.0f;

		// WARNING: AllowOverlap keeps a click reaching the one Selectable under the row instead of being swallowed by the cells drawn over it.
		constexpr ImGuiSelectableFlags kRowSelectableFlags = ImGuiSelectableFlags_AllowOverlap;

		constexpr const char* kHashSample = "0000000";
		constexpr const char* kAuthorSample = "Alice Johnson   ";
		constexpr const char* kDateSample = "59 minutes ago";

		constexpr std::string_view kLocalChangesTitle = "Local changes";
		constexpr std::string_view kLocalChangesAge = "now";
		constexpr const char* kBroughtWaiting = "Reading the commits this merge brought\xE2\x80\xA6";
		constexpr const char* kBroughtFailed = "Could not read the commits this merge brought";
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
			Hash,
			Message,
			Author,
			Date,
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
		void KeepClearOfScrollbar(float gap)
		{
			ImGuiTable* table = ImGui::GetCurrentTable();

			if (table == nullptr || !table->InnerWindow->ScrollbarY)
			{
				return;
			}

			const float right = std::max(table->WorkRect.Min.x, table->WorkRect.Max.x - gap);

			table->WorkRect.Max.x = right;
			table->InnerClipRect.Max.x = std::min(table->InnerClipRect.Max.x, right);
			table->HostClipRect.Max.x = std::min(table->HostClipRect.Max.x, right);
		}

		float MinMessageWidth()
		{
			return ImGui::CalcTextSize("0").x * kMinMessageDigitCount;
		}

		float SampleWidth(const char* sample)
		{
			return ImGui::CalcTextSize(sample).x;
		}

		// WARNING: The hash is drawn in the code face at the interface size, so a row keeps the pitch the graph is laid out on; the column is sized in the same face or the hash is cut.
		float HashSampleWidth()
		{
			Core::PushCodeFont(Core::kInterfaceFontSize);
			const float width = SampleWidth(kHashSample);
			ImGui::PopFont();

			return width;
		}

		// WARNING: WidthRequest holds the reader's drag or the width saved in the settings, and a hidden column keeps it.
		float WidthInUse(const ImGuiTable* table, Column column, float usualWidth)
		{
			const float requestedWidth = table->Columns[TableIndexOf(column)].WidthRequest;
			return requestedWidth > 0.0f ? requestedWidth : usualWidth;
		}

		void DrawFittedCell(std::string_view text, ImU32 color, std::string_view needle)
		{
			if (text.empty())
			{
				return;
			}

			const float roomInCell = ImGui::GetContentRegionAvail().x + kRoundedUpPixel;
			const float ellipsisWidth = Core::TextWidth(kEllipsis);

			const std::optional<std::size_t> cut = CutToFit(text, roomInCell, ellipsisWidth, Core::TextWidth);

			// WARNING: Nothing rather than an ellipsis the cell would clip, as FitToWidth does.
			if (cut.has_value() && ellipsisWidth > roomInCell)
			{
				return;
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
		}

		void RefLabels(const std::vector<CommitRef>& refs, const GitPalette& palette,
					   std::string_view summary, float alpha)
		{
			if (refs.empty())
			{
				return;
			}

			const ImGuiStyle& style = ImGui::GetStyle();
			const float labelPadding = style.ItemInnerSpacing.x;
			const float labelGap = style.ItemInnerSpacing.x;

			std::vector<float> labelWidths;
			labelWidths.reserve(refs.size());

			for (const CommitRef& ref : refs)
			{
				labelWidths.push_back(Core::TextWidth(ref.Name) + labelPadding * 2.0f);
			}

			const std::size_t fittingCount = RefLabelsThatFit(
				labelWidths, labelGap, ImGui::GetContentRegionAvail().x + kRoundedUpPixel,
				Core::TextWidth(summary), MinMessageWidth());

			ImDrawList* drawList = ImGui::GetWindowDrawList();

			for (std::size_t labelIndex = 0; labelIndex < fittingCount; ++labelIndex)
			{
				const CommitRef& ref = refs[labelIndex];

				ImU32 ink = ImGui::GetColorU32(ImGuiCol_TextDisabled);

				if (ref.Kind == CommitRefKind::Head)
				{
					ink = palette.GutterAdded;
				}
				else if (ref.Kind == CommitRefKind::Local)
				{
					ink = palette.GutterModified;
				}

				const ImU32 fill = Core::WithAlphaScaled(Core::WithAlphaReplaced(ink, kLabelFillAlphaByte), alpha);
				ink = Core::WithAlphaScaled(ink, alpha);

				const ImVec2 labelOrigin = ImGui::GetCursorScreenPos();
				const ImVec2 labelSize(labelWidths[labelIndex], ImGui::GetTextLineHeight());

				drawList->AddRectFilled(
					labelOrigin, ImVec2(labelOrigin.x + labelSize.x, labelOrigin.y + labelSize.y),
					fill, style.FrameRounding);
				drawList->AddText(ImVec2(labelOrigin.x + labelPadding, labelOrigin.y), ink,
								  ref.Name.data(), ref.Name.data() + ref.Name.size());

				ImGui::Dummy(labelSize);
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

			case Column::Date:
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

		const bool hasFold = marks.Fold.has_value() && marks.Fold->BeforeCommit < page.Commits.size();
		const ExtraRows extra{.LocalChanges = marks.LocalChanges.has_value(),
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

		// WARNING: Without NoBordersInBody ImGui puts each resize handle down the whole list, so a click near a border on any row would drag the column instead of selecting the commit.
		constexpr ImGuiTableFlags flags =
			ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;

		const ImGuiStyle& style = ImGui::GetStyle();

		const float widthBesideScrollbar = ImGui::GetContentRegionAvail().x - style.ScrollbarSize;

		if (ImGui::BeginTable("##commits", TableIndexOf(Column::Count), flags, ImVec2(0.0f, -m_RoomBelow)))
		{
			PadListTableEnds();
			KeepClearOfScrollbar(ImGui::GetStyle().ItemSpacing.x);

			const ImGuiTable* table = ImGui::GetCurrentTable();

			CommitColumnsInput columnInput;
			columnInput.Available = widthBesideScrollbar;
			columnInput.Graph = GraphWidth(metrics, laneCount);
			columnInput.Chevron = std::max(SampleWidth(ChevronGlyph(false)), SampleWidth(ChevronGlyph(true)));
			columnInput.Hash = WidthInUse(table, Column::Hash, HashSampleWidth());
			columnInput.Author = WidthInUse(table, Column::Author, SampleWidth(kAuthorSample));
			columnInput.Date = WidthInUse(table, Column::Date, SampleWidth(kDateSample));
			columnInput.CellPadding = style.CellPadding.x * 2.0f;
			columnInput.MinMessage = MinMessageWidth();

			const CommitColumns columns = LayoutCommitColumns(columnInput);

			constexpr ImGuiTableColumnFlags kFixedWidth = ImGuiTableColumnFlags_WidthFixed;
			constexpr ImGuiTableColumnFlags kFixedAndLocked =
				ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize;

			ImGui::TableSetupColumn("##graph", kFixedAndLocked, columns.Graph);
			ImGui::TableSetupColumn("##chevron", kFixedAndLocked, columnInput.Chevron);
			ImGui::TableSetupColumn("Hash", kFixedWidth, HashSampleWidth());
			ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);

			// WARNING: A narrow table gives up the author and then the date with the Disabled flag, which is read again every frame. The table is not Hideable, so ImGui offers no menu to bring them back and does not save the state.
			const ImGuiTableColumnFlags authorFlags =
				columns.ShowAuthor ? kFixedWidth : kFixedWidth | ImGuiTableColumnFlags_Disabled;
			const ImGuiTableColumnFlags dateFlags =
				columns.ShowDate ? kFixedWidth : kFixedWidth | ImGuiTableColumnFlags_Disabled;

			ImGui::TableSetupColumn("Author", authorFlags, SampleWidth(kAuthorSample));
			ImGui::TableSetupColumn("Date", dateFlags, SampleWidth(kDateSample));

			ImGui::TableSetupScrollFreeze(kFrozenColumnCount, kFrozenRowCount);
			ImGui::TableHeadersRow();

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
					case CommitRowKind::Commit:
					{
						const Core::CommitSummary& commit = page.Commits[row.Commit];
						const bool dimmed = marks.Dimmed != nullptr && marks.Dimmed->contains(commit.Id);

						const Core::Ref<const Core::LogPage> brought =
							opensBrought(row.Commit) ? marks.BroughtOf(commit.Id) : nullptr;

						RenderCommitRow(page, commit, palette, refs, highlight, now, zone, dimmed,
										BroughtDrawing{.Page = brought.get(), .IsBrought = false});

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
							RenderBroughtNote(brought.get(), !brought && marks.BroughtFailed && marks.BroughtFailed(merge.Id));
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

					CommitGraphView{}.Render(graph, palette, metrics, rows.RowOfCommit,
											 clipper.DisplayStart, clipper.DisplayEnd - 1,
											 graphOrigin);
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

		// A commit a merge brought does not open files.
		if (!brought.IsBrought)
		{
			DrawChevron(expanded);
		}

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Hash));
		const float chevronZoneEndX = ImGui::GetCursorScreenPos().x;

		// A dimmed row is dimmed whole, and less while it is pointed at or selected.
		const float alpha =
			!dimmed ? 1.0f
					: (isSelectedCommit || selectableHovered ? CommitListView::kDimmedRowPointedAlpha
															 : CommitListView::kDimmedRowAlpha);

		const ImU32 dimmedInk = Core::WithAlphaScaled(ImGui::GetColorU32(ImGuiCol_TextDisabled), alpha);
		const ImU32 hashInk = page.Unpushed.contains(commit.Id) ? Core::WithAlphaScaled(palette.GutterAdded, alpha) : dimmedInk;
		const ImU32 summaryInk = dimmed ? Core::WithAlphaScaled(ImGui::GetColorU32(ImGuiCol_Text), alpha) : kThemeTextColor;

		Core::PushCodeFont(Core::kInterfaceFontSize);
		DrawFittedCell(commit.Id.ShortHex(), hashInk, highlight);
		ImGui::PopFont();

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));

		if (brought.IsBrought)
		{
			DrawBroughtElbow();
			ImGui::Indent(MockupPixels(kBroughtIndentPixels));
		}

		if (refs.contains(commit.Id))
		{
			RefLabels(refs.at(commit.Id), palette, commit.Summary, alpha);
		}

		// An opened merge says how many commits it brought after its own subject.
		const std::string summaryWithCount =
			brought.Page != nullptr ? commit.Summary + kMiddleDot + DescribeBrought(*brought.Page) : std::string{};

		DrawFittedCell(brought.Page != nullptr ? std::string_view{summaryWithCount} : std::string_view{commit.Summary},
					   summaryInk, highlight);

		if (brought.IsBrought)
		{
			ImGui::Unindent(MockupPixels(kBroughtIndentPixels));
		}

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Author));
		DrawFittedCell(AuthorName(commit.Author), dimmedInk, highlight);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Date));
		DrawFittedCell(RelativeAge(commit.Author.When, now), dimmedInk, kNoHighlight);

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

	void CommitListView::RenderBroughtNote(const Core::LogPage* brought, bool failed)
	{
		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));
		ImGui::Indent(MockupPixels(kBroughtIndentPixels));

		if (brought == nullptr && !failed)
		{
			Core::LoadingLine(kBroughtWaiting);
		}
		else
		{
			DrawFittedCell(failed ? kBroughtFailed : kBroughtNone, ImGui::GetColorU32(ImGuiCol_TextDisabled), kNoHighlight);
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

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Date));
		DrawFittedCell(kLocalChangesAge, dimmedInk, kNoHighlight);
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

		const float textLeft = ImGui::GetCursorScreenPos().x;

		// WARNING: The italic face is not loaded where no renderer is, and a null face keeps the current one.
		ImGui::PushFont(Core::ImGuiRenderer::GetItalicFont(), kKeepFontSize);
		const float textRoom = Core::TextWidth(fold.TextRoom + kMiddleDot);
		DrawFittedCell(fold.Text + kMiddleDot, dimmedInk, kNoHighlight);
		ImGui::PopFont();

		// The action keeps to the room the count may come to take, so it stays put when the count lands or changes.
		ImGui::SameLine(kNoOffset, kNoSpacing);
		const float roomLeft = std::max(0.0f, textLeft + textRoom - ImGui::GetCursorScreenPos().x);
		ImGui::Dummy(ImVec2(std::min(roomLeft, ImGui::GetContentRegionAvail().x), ImGui::GetTextLineHeight()));
		ImGui::SameLine(kNoOffset, kNoSpacing);

		const float actionLeft = ImGui::GetCursorScreenPos().x;
		DrawFittedCell(fold.Action, actionInk, kNoHighlight);

		if (hovered && ImGui::GetItemRectMax().x > actionLeft)
		{
			const float underline = std::floor(ImGui::GetItemRectMin().y + ImGui::GetFontSize());
			ImGui::GetWindowDrawList()->AddLine(ImVec2(actionLeft, underline), ImVec2(ImGui::GetItemRectMax().x, underline),
												actionInk, kUnderlineThickness);
		}
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
