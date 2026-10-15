#include "Git/History/CommitListView.h"

#include "Core/Base/Narrow.h"

#include "Core/Base/Base.h"

#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/FileSystem/FileSystem.h"

#include "Git/History/CommitColumns.h"
#include "Git/History/CommitDetails.h"
#include "Git/History/CommitLabels.h"
#include "Git/History/CommitRows.h"
#include "UI/HighlightedRow.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
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
		constexpr ImGuiSelectableFlags kRowSelectableFlags =
			ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;

		constexpr const char* kHashSample = "0000000";
		constexpr const char* kAuthorSample = "Alice Johnson   ";
		constexpr const char* kDateSample = "59 minutes ago";

		GraphMetrics MetricsForCurrentFont()
		{
			GraphMetrics metrics;
			metrics.LaneWidth = ImGui::GetTextLineHeight() * kLaneWidthPerLineHeight;
			metrics.RowHeight = ImGui::GetTextLineHeightWithSpacing();
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

		// WARNING: WidthRequest holds the reader's drag or the width saved in the settings, and a hidden column keeps it.
		float WidthInUse(const ImGuiTable* table, Column column, float usualWidth)
		{
			const float requestedWidth = table->Columns[TableIndexOf(column)].WidthRequest;
			return requestedWidth > 0.0f ? requestedWidth : usualWidth;
		}

		void FittedText(std::string_view text, ImU32 color, std::string_view needle)
		{
			if (text.empty())
			{
				return;
			}

			const float roomInCell = ImGui::GetContentRegionAvail().x + kRoundedUpPixel;

			const std::optional<std::size_t> cut =
				CutToFit(text, roomInCell, Core::TextWidth(Ellipsis), Core::TextWidth);

			const HighlightedLabel split =
				SplitHighlight(cut.has_value() ? text.substr(0, *cut) : text, needle);

			const ImU32 ink = color != kThemeTextColor ? color : ImGui::GetColorU32(ImGuiCol_Text);
			const ImU32 mark = ImGui::GetColorU32(ImGuiCol_CheckMark);

			const TextPiece pieces[] = {
				{split.Before, ink},
				{split.Match, mark},
				{split.After, ink},
				{cut.has_value() ? Ellipsis : std::string_view{}, ink},
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
					   std::string_view summary)
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

				const ImU32 fill = Core::WithAlphaReplaced(ink, kLabelFillAlphaByte);

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
									std::chrono::minutes offset)
		{
			return std::format("{}  ·  {}", FormattedTimestamp(when, offset),
							   RelativeAge(when, now));
		}

		void CellTooltip(int column, const Core::CommitSummary& commit,
						 std::chrono::sys_seconds now, std::chrono::minutes offset)
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
				text = FormattedMoment(commit.Author.When, now, offset);

				if (wasCommittedAtAnotherTime)
				{
					text += "\ncommitted " + FormattedMoment(commit.Committer.When, now, offset);
				}

				break;

			default:
				return;
			}

			ImGui::SetTooltip("%s", text.c_str());
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

	void CommitListView::KeepSelectionWithin(const Core::LogPage& page)
	{
		m_SelectedFile = SelectionSurviving(m_SelectedFile, page);
		m_SelectedCommit = CommitSelectionSurviving(m_SelectedCommit, page);
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

	void CommitListView::Reset()
	{
		m_Expanded.clear();
		m_SelectedFile.reset();
		m_SelectedCommit.reset();
		ForgetLoadMoreRequest();
	}

	void CommitListView::ForgetLoadMoreRequest()
	{
		m_LoadMoreRequested = false;
		m_LoadMoreRequestedAtCount = kNoLoadMoreRequestedYet;
	}

	bool CommitListView::TakeLoadMoreRequest()
	{
		const bool requested = m_LoadMoreRequested;
		m_LoadMoreRequested = false;
		return requested;
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
								const GitPalette& palette, std::chrono::minutes offset,
								const std::function<Core::Ref<const Core::Diff>(const Core::Oid&)>& diffOf,
								const std::function<bool(const Core::Oid&)>& diffFailed,
								const CommitRefMap& refs, std::string_view highlight,
								std::string_view pathHighlight,
								const std::optional<ForkSeparator>& separator)
	{
		if (page.Commits.empty())
		{
			Core::EmptyState("No commits yet.");
			return;
		}

		const GraphMetrics metrics = MetricsForCurrentFont();

		const bool hasSeparator = separator.has_value() && separator->BeforeCommit < page.Commits.size();

		const CommitRows rows = BuildCommitRows(
			page.Commits.size(),
			[&](std::size_t commitIndex) { return FileRowCountUnder(page, commitIndex, diffOf); },
			hasSeparator ? std::optional<std::size_t>{separator->BeforeCommit} : std::nullopt);

		const std::optional<std::size_t> separatorRow =
			hasSeparator ? std::optional<std::size_t>{rows.RowOfCommit[separator->BeforeCommit] - 1} : std::nullopt;

		const std::chrono::sys_seconds now =
			std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

		// WARNING: A table row is as tall as its tallest cell plus the cell padding, so the padding is half the item spacing on each side: one line of text plus both halves is RowHeight, which the clipper seeks by and the graph is placed by. The minimum height passed to each row says the same again, so a shorter cell cannot pull the rows up away from the graph.
		const float cellPaddingY = ImGui::GetStyle().ItemSpacing.y * kHalf;

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
							ImVec2(ImGui::GetStyle().CellPadding.x, cellPaddingY));

		// WARNING: Without NoBordersInBody ImGui puts each resize handle down the whole list, so a click near a border on any row would drag the column instead of selecting the commit. Without PadOuterX the last column's text runs into the scrollbar.
		constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
										  ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY |
										  ImGuiTableFlags_PadOuterX;

		const ImGuiStyle& style = ImGui::GetStyle();

		const float widthBesideScrollbar = ImGui::GetContentRegionAvail().x - style.ScrollbarSize;

		if (ImGui::BeginTable("##commits", TableIndexOf(Column::Count), flags))
		{
			KeepClearOfScrollbar(ImGui::GetStyle().ItemSpacing.x);

			const ImGuiTable* table = ImGui::GetCurrentTable();

			CommitColumnsInput columnInput;
			columnInput.Available = widthBesideScrollbar;
			columnInput.Graph = GraphWidth(metrics, graph.LaneCount);
			columnInput.Chevron = std::max(SampleWidth(ICON_MDI_CHEVRON_RIGHT),
										   SampleWidth(ICON_MDI_CHEVRON_DOWN));
			columnInput.Hash = WidthInUse(table, Column::Hash, SampleWidth(kHashSample));
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
			ImGui::TableSetupColumn("Hash", kFixedWidth, SampleWidth(kHashSample));
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

			const int rowCount = Core::Narrow<int>(rows.Rows.size());
			bool lastRowDrawn = false;

			ImGuiListClipper clipper;
			clipper.Begin(rowCount, metrics.RowHeight);

			while (clipper.Step())
			{
				lastRowDrawn = lastRowDrawn || clipper.DisplayEnd == rowCount;

				// WARNING: Display row 0 is usually scrolled far above the table and nothing is drawn at it, so its position is worked back from a row drawn in this step.
				ImVec2 graphOrigin;

				for (int displayRow = clipper.DisplayStart; displayRow < clipper.DisplayEnd;
					 ++displayRow)
				{
					const CommitRow& row = rows.Rows[static_cast<std::size_t>(displayRow)];
					const Core::CommitSummary& commit = page.Commits[row.Commit];

					ImGui::TableNextRow(ImGuiTableRowFlags_None, metrics.RowHeight);
					ImGui::PushID(displayRow);

					ImGui::TableSetColumnIndex(TableIndexOf(Column::Graph));
					const ImVec2 graphCell = ImGui::GetCursorScreenPos();
					graphOrigin = ImVec2(graphCell.x, graphCell.y - cellPaddingY -
														  static_cast<float>(displayRow) * metrics.RowHeight);

					if (row.Kind == CommitRowKind::Separator)
					{
						RenderSeparatorRow(separator->Text);
					}
					else if (row.Kind == CommitRowKind::Commit)
					{
						const bool isEarlierHistory = separatorRow.has_value() && static_cast<std::size_t>(displayRow) > *separatorRow;

						RenderCommitRow(page, commit, palette, refs, highlight, now, offset, isEarlierHistory);
					}
					else
					{
						RenderFileRow(commit, row.File, palette, diffOf, diffFailed, pathHighlight);
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

			// WARNING: The end is reached when the clipper draws the last row, not when the scroll reaches its maximum: until the table has measured its rows that maximum is 0, so a page just shown would ask for more before anyone scrolled. The request is latched on the page's commit count, so it is not re-armed every frame the reader stays at the end while the bigger page is fetched.
			const std::ptrdiff_t commitCount = static_cast<std::ptrdiff_t>(page.Commits.size());

			if (!page.Complete && lastRowDrawn && commitCount != m_LoadMoreRequestedAtCount)
			{
				m_LoadMoreRequested = true;
				m_LoadMoreRequestedAtCount = commitCount;
			}

			ImGui::EndTable();
		}

		ImGui::PopStyleVar();
	}

	void CommitListView::RenderCommitRow(const Core::LogPage& page, const Core::CommitSummary& commit,
										 const GitPalette& palette, const CommitRefMap& refs,
										 std::string_view highlight, std::chrono::sys_seconds now,
										 std::chrono::minutes offset, bool isEarlierHistory)
	{
		const bool expanded = IsExpanded(commit.Id);
		const bool isSelectedCommit = m_SelectedCommit == commit.Id;

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Chevron));

		const float chevronZoneStartX =
			ImGui::GetCursorScreenPos().x - ImGui::GetStyle().CellPadding.x;

		const ImVec2 rowStart = ImGui::GetCursorPos();

		const bool selectableClicked =
			ImGui::Selectable("##commit", isSelectedCommit, kRowSelectableFlags);

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
		ImGui::TextUnformatted(expanded ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Hash));
		const float chevronZoneEndX = ImGui::GetCursorScreenPos().x;

		const ImU32 dimmedInk = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const ImU32 hashInk = page.Unpushed.contains(commit.Id) ? palette.GutterAdded : dimmedInk;

		FittedText(commit.Id.ShortHex(), hashInk, highlight);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));

		if (refs.contains(commit.Id))
		{
			RefLabels(refs.at(commit.Id), palette, commit.Summary);
		}

		FittedText(commit.Summary, isEarlierHistory ? dimmedInk : kThemeTextColor, highlight);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Author));
		FittedText(AuthorName(commit.Author), dimmedInk, highlight);

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Date));
		FittedText(RelativeAge(commit.Author.When, now), dimmedInk, kNoHighlight);

		if (selectableHoveredForTooltip)
		{
			CellTooltip(ImGui::TableGetHoveredColumn(), commit, now, offset);
		}

		if (!selectableClicked)
		{
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
			m_SelectedCommit = commit.Id;
			m_SelectedFile.reset();
		}
	}

	void CommitListView::RenderSeparatorRow(const std::string& text)
	{
		ImGui::TableSetColumnIndex(TableIndexOf(Column::Message));
		FittedText(text, ImGui::GetColorU32(ImGuiCol_TextDisabled), kNoHighlight);
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
				ImGui::TextDisabled("Loading…");
			}
			else if (diff->Files.empty())
			{
				ImGui::TextDisabled("No file changes");
			}
			else
			{
				ImGui::TextDisabled("%s", std::string(Ellipsis).c_str());
			}

			return;
		}

		const std::filesystem::path& path = ShownPath(*changedFile);

		const bool isSelectedFile = m_SelectedFile.has_value() &&
									m_SelectedFile->Commit == commit.Id &&
									m_SelectedFile->Path == path;

		ImGui::TableSetColumnIndex(TableIndexOf(Column::Chevron));

		if (ImGui::Selectable("##file", isSelectedFile, kRowSelectableFlags))
		{
			m_SelectedFile = FileSelection{commit.Id, path};
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

		FittedText(letter, ChangeColor(changedFile->Change, palette), kNoHighlight);

		if (const char* changeName = ChangeName(changedFile->Change);
			changeName != nullptr && letterFits)
		{
			ImGui::SetItemTooltip("%s", changeName);
		}

		ImGui::SameLine(kNoOffset, kNoSpacing);
		FittedText(std::format("  {}", Core::ToUtf8(path)), kThemeTextColor, pathHighlight);
	}
}
