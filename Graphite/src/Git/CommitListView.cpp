#include "Git/CommitListView.h"

#include "Git/CommitDetails.h"
#include "Git/CommitLabels.h"
#include "Git/CommitRows.h"
#include "UI/HighlightedRow.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	namespace {

		GraphMetrics MetricsForCurrentFont()
		{
			GraphMetrics metrics;
			metrics.LaneWidth = ImGui::GetTextLineHeight() * 0.8f;
			metrics.RowHeight = ImGui::GetTextLineHeightWithSpacing();
			metrics.DotRadius = metrics.LaneWidth * 0.22f;
			return metrics;
		}

		// The table's columns, in the order they are set up.
		enum Column : int
		{
			GraphColumn,
			ChevronColumn,
			HashColumn,
			MessageColumn,
			AuthorColumn,
			DateColumn,
			ColumnCount
		};

		float WidthOf(std::string_view text)
		{
			// Guarded because an empty view may carry no pointer at all, and CalcTextSize
			// reads a null end as "up to the terminator".
			return text.empty() ? 0.0f
								: ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
		}

		// `text` in `color` — zero for the theme's own — with the needle marked, cut with an
		// ellipsis to whatever is left of the current cell.
		//
		// Measured with CalcTextSize, which rounds up: at worst a text that would just have
		// fitted is cut a character early, which is the harmless direction for a cut.
		void FittedText(std::string_view text, ImU32 color, std::string_view needle)
		{
			if (text.empty())
			{
				return;
			}

			const std::optional<std::size_t> cut =
				CutToFit(text, ImGui::GetContentRegionAvail().x, WidthOf(Ellipsis), WidthOf);

			const HighlightedLabel split =
				SplitHighlight(cut.has_value() ? text.substr(0, *cut) : text, needle);

			const ImU32 ink = color != 0 ? color : ImGui::GetColorU32(ImGuiCol_Text);
			const ImU32 mark = ImGui::GetColorU32(ImGuiCol_CheckMark);

			const struct
			{
				std::string_view Text;
				ImU32 Color;
			} pieces[] = {
				{split.Before, ink},
				{split.Match, mark},
				{split.After, ink},
				{cut.has_value() ? Ellipsis : std::string_view{}, ink},
			};

			// An empty piece is skipped rather than drawn: it has nothing to show, and
			// drawing it would only put one more item on the line.
			bool first = true;

			for (const auto& piece : pieces)
			{
				if (piece.Text.empty())
				{
					continue;
				}

				if (!first)
				{
					ImGui::SameLine(0.0f, 0.0f);
				}

				first = false;

				ImGui::PushStyleColor(ImGuiCol_Text, piece.Color);
				ImGui::TextUnformatted(piece.Text.data(), piece.Text.data() + piece.Text.size());
				ImGui::PopStyleColor();
			}
		}

		// What the cell under the pointer leaves out. The summary has none: the whole
		// message is what selecting the commit shows in the pane below.
		void CellTooltip(int column, const Core::CommitSummary& commit,
						 std::chrono::sys_seconds now, std::chrono::minutes offset)
		{
			const auto signature = [](const Core::Signature& who) {
				return std::format("{} <{}>", who.Name, who.Email);
			};

			const auto moment = [&](std::chrono::sys_seconds when) {
				return std::format("{}  \xC2\xB7  {}", FormattedTimestamp(when, offset),
								   RelativeAge(when, now));
			};

			std::string text;

			switch (column)
			{
			case HashColumn:
				text = commit.Id.ToHex();
				break;

			case AuthorColumn:
				text = signature(commit.Author);

				if (ShowsCommitter(commit))
				{
					text += "\ncommitted by " + signature(commit.Committer);
				}

				break;

			case DateColumn:
				text = moment(commit.Author.When);

				// A rebased or amended commit keeps its author's date and gains a later one
				// of its own, and a reader asking when a commit happened may mean either.
				if (commit.Committer.When != commit.Author.When)
				{
					text += "\ncommitted " + moment(commit.Committer.When);
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
		if (m_Expanded.erase(commit))
		{
			// The row the selection pointed at just left the list. A selection naming it
			// would leave the diff pane showing a file that appears nowhere above it, and
			// GitView has already dropped the cached diff for this commit — so nothing
			// would ever answer it either. Clearing it here, in the class that owns both
			// the expansion set and the selection, is what lets "Select a file to see its
			// changes." stay honest about what is on screen.
			if (m_SelectedFile.has_value() && m_SelectedFile->Commit == commit)
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
		m_LoadMoreRequestedAtCount = -1;
	}

	bool CommitListView::TakeLoadMoreRequest()
	{
		const bool requested = m_LoadMoreRequested;
		m_LoadMoreRequested = false;
		return requested;
	}

	void CommitListView::Render(const Core::LogPage& page, const Core::CommitGraph& graph,
								const GitPalette& palette, std::chrono::minutes offset,
								const std::function<std::shared_ptr<const Core::Diff>(const Core::Oid&)>& diffOf,
					std::string_view highlight, std::string_view pathHighlight)
	{
		if (page.Commits.empty())
		{
			ImGui::TextUnformatted("No commits yet.");
			return;
		}

		const GraphMetrics metrics = MetricsForCurrentFont();
		const float graphWidth = GraphWidth(metrics, graph.LaneCount);

		const CommitRows rows = BuildCommitRows(page.Commits.size(), [&](std::size_t commit) {
			const Core::CommitSummary& summary = page.Commits[commit];

			if (!IsExpanded(summary.Id))
			{
				return std::size_t{0};
			}

			const std::shared_ptr<const Core::Diff> diff = diffOf(summary.Id);

			return diff ? diff->Files.size() : std::size_t{0};
		});

		const auto now = std::chrono::floor<std::chrono::seconds>(
			std::chrono::system_clock::now());

		// Every row is exactly metrics.RowHeight tall — the height the clipper seeks by and
		// CommitGraphView places the graph by. A table row is as tall as its tallest cell
		// plus the cell padding above and below it, so the padding is half the item spacing
		// each side: one line of text plus both halves is the font's line height with
		// spacing, which is RowHeight. The minimum height passed to each row says the same
		// thing again, so a cell that ever came out shorter could not pull the rows below it
		// up and away from the graph.
		//
		// Half above and half below, rather than all of it below, is also what lines the row
		// up with its own selection highlight: a Selectable spanning the columns reaches half
		// the spacing past its text on both sides — rounded down, so with an odd spacing the
		// highlight sits half a pixel off the stripe, which nobody can see.
		const float cellPaddingY = ImGui::GetStyle().ItemSpacing.y * 0.5f;

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
							ImVec2(ImGui::GetStyle().CellPadding.x, cellPaddingY));

		// The table scrolls itself, filling the window it is drawn in, so the header row can
		// stay at the top while the history moves under it. The header is also the only
		// place a column is resized from: without NoBordersInBody, ImGui puts each resize
		// handle down the whole height of the list, and a click a few pixels either side of a
		// border — on any row — would drag the column instead of selecting the commit.
		//
		// A resizable table keeps its widths in the ImGui settings, so a column dragged wider
		// stays that way the next time the application opens.
		constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
										  ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;

		if (ImGui::BeginTable("##commits", ColumnCount, flags))
		{
			const auto widthOf = [](const char* sample) { return ImGui::CalcTextSize(sample).x; };

			// The lanes and the arrow are not the reader's to resize: the graph is as wide as
			// the history has lanes, and ImGui re-applies that width every frame to a fixed
			// column that cannot be resized, so it follows the history as branches come and go.
			ImGui::TableSetupColumn("##graph",
									ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
									graphWidth);
			ImGui::TableSetupColumn("##chevron",
									ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
									std::max(widthOf(ICON_MDI_CHEVRON_RIGHT),
											 widthOf(ICON_MDI_CHEVRON_DOWN)));

			// The rest open at the width of what they usually hold. A cell too narrow for its
			// text cuts it with an ellipsis, and the tooltip over it says the rest.
			ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, widthOf("0000000"));
			ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthFixed,
									widthOf("Alice Johnson   "));
			ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed,
									widthOf("59 minutes ago"));

			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(rows.Rows.size()), metrics.RowHeight);

			while (clipper.Step())
			{
				// Where display row 0 is, worked back from a row drawn in this step. Row 0 is
				// usually scrolled far above the table, and nothing is drawn at it to ask.
				ImVec2 graphOrigin;

				for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
				{
					const CommitRow& row = rows.Rows[static_cast<std::size_t>(index)];
					const Core::CommitSummary& commit = page.Commits[row.Commit];

					ImGui::TableNextRow(ImGuiTableRowFlags_None, metrics.RowHeight);
					ImGui::PushID(index);

					ImGui::TableSetColumnIndex(GraphColumn);
					const ImVec2 cell = ImGui::GetCursorScreenPos();
					graphOrigin = ImVec2(cell.x, cell.y - cellPaddingY -
													 static_cast<float>(index) * metrics.RowHeight);

					if (row.Kind == CommitRowKind::Commit)
					{
						RenderCommitRow(page, commit, palette, highlight, now, offset);
					}
					else
					{
						RenderFileRow(commit, row.File, palette, diffOf, pathHighlight);
					}

					ImGui::PopID();
				}

				// Drawn once per step, from inside the graph column of the step's last row —
				// still open, since the next row has not begun — so the lanes land in that
				// column's clip rect, which stops below the header row. A step is a run of
				// consecutive rows, and a line leaving it is drawn on to wherever it goes and
				// clipped there, so a second step drawing the same line again changes nothing.
				if (clipper.DisplayEnd > clipper.DisplayStart)
				{
					ImGui::TableSetColumnIndex(GraphColumn);

					CommitGraphView{}.Render(graph, palette, metrics, rows.RowOfCommit,
											 clipper.DisplayStart, clipper.DisplayEnd - 1,
											 graphOrigin);
				}
			}

			// Asking is what makes the difference between "this is all of it" and "there is
			// more, we just stopped looking" — the page says which, and a reader at the bottom
			// of an incomplete page wants the rest. Armed only once per page: the count is the
			// thing that actually changes when a request is answered, so latching on it
			// (rather than merely consuming the flag on read) stops the widget re-arming every
			// frame the reader happens to stay at the bottom while the bigger page is still
			// being fetched.
			//
			// Read before EndTable: until then the current window is the table's own scrolling
			// one, which is the scroll position that means "at the bottom of the history".
			const std::ptrdiff_t commitCount = static_cast<std::ptrdiff_t>(page.Commits.size());

			if (!page.Complete && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() &&
				commitCount != m_LoadMoreRequestedAtCount)
			{
				m_LoadMoreRequested = true;
				m_LoadMoreRequestedAtCount = commitCount;
			}

			ImGui::EndTable();
		}

		ImGui::PopStyleVar();
	}

	void CommitListView::RenderCommitRow(const Core::LogPage& page, const Core::CommitSummary& commit,
										 const GitPalette& palette, std::string_view highlight,
										 std::chrono::sys_seconds now, std::chrono::minutes offset)
	{
		const bool expanded = IsExpanded(commit.Id);

		// Selected rather than merely expanded is what the row draws as highlighted:
		// expansion is already said by the chevron and by the file rows below, while the
		// selection is the only thing that says which commit the pane underneath is
		// describing.
		const bool selected = m_SelectedCommit == commit.Id;

		ImGui::TableSetColumnIndex(ChevronColumn);

		// The arrow's zone starts at the edge of its own column, padding included. The
		// Selectable below spans the graph column too, and a click on the lanes is a click
		// on the commit, not on its arrow.
		const float chevronStartX = ImGui::GetCursorScreenPos().x - ImGui::GetStyle().CellPadding.x;

		// One Selectable under the whole row, with every cell drawn over it: the row is one
		// thing to click, whichever column the click lands in. AllowOverlap keeps the click
		// reaching it rather than being swallowed by the text laid on top.
		const ImVec2 rowStart = ImGui::GetCursorPos();

		const bool clicked = ImGui::Selectable(
			"##commit", selected,
			ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);

		// Asked here, of the Selectable itself: every cell below becomes the last item in
		// turn, and asking afterwards would ask the last piece of text instead.
		const bool hovered = ImGui::IsItemHovered();
		const bool hoveredForTooltip = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);

		ImGui::SetCursorPos(rowStart);
		ImGui::TextUnformatted(expanded ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT);

		// Everything left of the hash is the arrow's to click, the padding between the two
		// columns included: forgiving to hit, and never reaching a click meant for the row.
		ImGui::TableSetColumnIndex(HashColumn);
		const float hashStartX = ImGui::GetCursorScreenPos().x;

		// Hash, author and date are dimmed so the summary, the part of the row that is read
		// rather than looked up, is the one thing at full strength.
		const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);

		// A commit the upstream does not have yet: what a push would send. Said with the
		// colour the editor's gutter gives an added line — an unpushed commit is one the
		// remote has yet to gain — and said on the hash, which is the part of the row that
		// names the commit rather than describing it.
		const ImU32 hashColor = page.Unpushed.contains(commit.Id) ? palette.GutterAdded : dim;

		// Whatever the history was filtered by is marked wherever it lands in the row —
		// hash, summary or author — because the filter matches all three and the row is
		// where the answer to "why this commit" is.
		FittedText(commit.Id.ShortHex(), hashColor, highlight);

		ImGui::TableSetColumnIndex(MessageColumn);
		FittedText(commit.Summary, 0, highlight);

		ImGui::TableSetColumnIndex(AuthorColumn);
		FittedText(AuthorName(commit.Author), dim, highlight);

		ImGui::TableSetColumnIndex(DateColumn);
		FittedText(RelativeAge(commit.Author.When, now), dim, {});

		if (hoveredForTooltip)
		{
			CellTooltip(ImGui::TableGetHoveredColumn(), commit, now, offset);
		}

		if (!clicked)
		{
			return;
		}

		// Where the press landed, not where the pointer is now: a Selectable fires on
		// release, and a press that started on the arrow is what the reader meant even if
		// the pointer drifted before they let go. Hover is checked too, so an activation
		// that did not come from the mouse — which leaves MouseClickedPos wherever it last
		// was — falls to selecting, the harmless of the two.
		const float pressedX = ImGui::GetIO().MouseClickedPos[0].x;
		const bool onChevron = hovered && pressedX >= chevronStartX && pressedX < hashStartX;

		if (onChevron)
		{
			// The arrow does this and nothing else: opening a commit's file list and reading
			// its message are separate questions, and a reader who wanted the other one can
			// ask for it without undoing this.
			ToggleExpanded(commit.Id);
		}
		else
		{
			m_SelectedCommit = commit.Id;

			// The file the pane was showing belongs to whichever commit was clicked last
			// time. Leaving it standing would put a diff from one commit under the row of
			// another, and the reader asked about this one.
			m_SelectedFile.reset();
		}
	}

	void CommitListView::RenderFileRow(
		const Core::CommitSummary& commit, std::size_t fileIndex, const GitPalette& palette,
		const std::function<std::shared_ptr<const Core::Diff>(const Core::Oid&)>& diffOf,
		std::string_view pathHighlight)
	{
		const std::shared_ptr<const Core::Diff> diff = diffOf(commit.Id);

		// The row layout was built from this same diffOf a moment ago; a file row only fails
		// to resolve if the cache evicted the entry in between, which a fixed-size cache can
		// do.
		const Core::FileDiff* file =
			(diff && fileIndex < diff->Files.size()) ? &diff->Files[fileIndex] : nullptr;

		if (file == nullptr)
		{
			// Still a row, drawn disabled: the row's minimum height keeps it as tall as the
			// others, so the graph stays on the rows it labels.
			ImGui::TableSetColumnIndex(MessageColumn);
			ImGui::TextDisabled("%s", std::string(Ellipsis).c_str());
			return;
		}

		const std::filesystem::path& path = ShownPath(*file);

		const bool selected = m_SelectedFile.has_value() && m_SelectedFile->Commit == commit.Id &&
							  m_SelectedFile->Path == path;

		ImGui::TableSetColumnIndex(ChevronColumn);

		if (ImGui::Selectable("##file", selected,
							  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
		{
			m_SelectedFile = FileSelection{commit.Id, path};
		}

		// Under the summaries, so a commit's files read as the continuation of its message
		// rather than as a column of their own.
		ImGui::TableSetColumnIndex(MessageColumn);

		// The letter carries the colour the editor's gutter gives that same change, so a
		// glance down the column says what happened without reading a letter per row.
		const std::string letter(1, ChangeLetter(file->Change));

		FittedText(letter, ChangeColor(file->Change, palette), {});
		ImGui::SameLine(0.0f, 0.0f);
		FittedText(std::format("  {}", path.string()), 0, pathHighlight);
	}
}
