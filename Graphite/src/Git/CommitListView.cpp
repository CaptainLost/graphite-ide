#include "Git/CommitListView.h"

#include "Git/CommitLabels.h"
#include "Git/CommitRows.h"
#include "UI/HighlightedRow.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>

#include <chrono>
#include <cstddef>
#include <format>
#include <string>

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
								const GitPalette& palette,
								const std::function<std::shared_ptr<const Core::Diff>(const Core::Oid&)>& diffOf,
					std::string_view highlight, std::string_view pathHighlight)
	{
		if (page.Commits.empty())
		{
			ImGui::TextUnformatted("No commits yet.");
			return;
		}

		// Draws into whatever window is already current rather than opening a child of its own:
		// the Git view calls this as DrawSplitResults's DrawResults callback, which opens one
		// sized to the split the user dragged. A second child here would nest inside that one,
		// with its own scrollbar and its own scroll position — and the load-more check below
		// reads ImGui::GetScrollY()/GetScrollMaxY() against whichever window is current, so it
		// has to be the caller's.
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

		// The graph is drawn once, under the whole list, from the list's own origin — so
		// the two cannot disagree about where row N is.
		const ImVec2 listOrigin = ImGui::GetCursorScreenPos();

		const auto now = std::chrono::floor<std::chrono::seconds>(
			std::chrono::system_clock::now());

		int firstDrawn = static_cast<int>(rows.Rows.size());
		int lastDrawn = 0;

		// Every row here is a plain Selectable at the font's own line height — the same
		// height metrics.RowHeight uses to seek the clipper and CommitGraphView uses to
		// place the graph. A control with its own fixed height, such as an ArrowButton,
		// would advance a commit row past that height and drift the graph away from the
		// row it labels, a pixel or more per row.
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(rows.Rows.size()), metrics.RowHeight);

		while (clipper.Step())
		{
			firstDrawn = clipper.DisplayStart < firstDrawn ? clipper.DisplayStart : firstDrawn;
			lastDrawn = clipper.DisplayEnd > lastDrawn ? clipper.DisplayEnd - 1 : lastDrawn;

			for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
			{
				const CommitRow& row = rows.Rows[static_cast<std::size_t>(index)];
				const Core::CommitSummary& commit = page.Commits[row.Commit];

				ImGui::PushID(index);
				ImGui::Indent(graphWidth);

				if (row.Kind == CommitRowKind::Commit)
				{
					const bool expanded = IsExpanded(commit.Id);

					// Sized to fit a typical two-word name such as "Alice Johnson" with a
					// couple of characters to spare; longer names are cut with an ellipsis.
					constexpr std::size_t AuthorColumnWidth = 16;

					// Sized to fit "59 minutes ago", the longest string RelativeAge returns
					// for the common case; the rarer multi-digit month/year ages get cut.
					constexpr std::size_t AgeColumnWidth = 14;

					// The arrow lives in the label rather than as its own ArrowButton, so a
					// commit row stays a plain Selectable — see the note above the clipper.
					// That is also why it is hit-tested by hand below: it has no item of its
					// own to ask.
					const char* chevron =
						expanded ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT;

					// Cut where the colour changes rather than built as one string: the
					// hash is tinted while the upstream does not have this commit yet, so it
					// is drawn as its own piece. The author and age columns are padded to a
					// fixed width so every summary starts at the same column in this
					// monospace list, rather than reading as ragged text.
					const std::string lead = std::format("{}  ", chevron);
					const std::string hash = commit.Id.ShortHex();
					const std::string rest =
						std::format("  {}  {}  {}", PaddedAuthor(commit.Author, AuthorColumnWidth),
									PaddedAge(commit.Author.When, now, AgeColumnWidth),
									commit.Summary);

					// Everything left of the hash: the arrow and the gap after it. Measured
					// from the label's own leading piece, so the zone follows the font
					// rather than a guess, and reaching as far as the hash makes the arrow
					// forgiving to hit without ever swallowing a click meant for the row.
					// CalcTextSize rounding up costs at most a pixel of gap, which is the
					// harmless direction for a hit zone.
					const float chevronZone =
						ImGui::CalcTextSize(std::format("{}  ", chevron).c_str()).x;

					const float rowStartX = ImGui::GetCursorScreenPos().x;

					// Whatever the history was filtered by is marked wherever it lands in the
					// row — hash, author or summary — because the filter matches all three
					// and the row is where the answer to "why this commit" is.
					//
					// Selected rather than merely expanded is what the row draws as
					// highlighted: expansion is already said by the chevron and by the file
					// rows below, while the selection is the only thing that says which
					// commit the pane underneath is describing.
					const bool selected = m_SelectedCommit == commit.Id;

					// A commit the upstream does not have yet: what a push would send. Said
					// with the colour the editor's gutter gives an added line — an unpushed
					// commit is one the remote has yet to gain — and said on the hash, which
					// is the part of the row that names the commit rather than describing
					// it. A column of its own would have had to be reserved on every row,
					// including the rows of a branch with nothing to push.
					const ImU32 tint =
						page.Unpushed.contains(commit.Id) ? palette.GutterAdded : 0;

					// Asked for here rather than read back with IsItemHovered below: a tinted
					// or filter-marked row is drawn as pieces laid over its Selectable, and
					// the last of those pieces is what ImGui would answer about.
					bool hovered = false;

					if (TintedSelectable("##commit", selected, lead, hash, tint, rest,
										 highlight, &hovered))
					{
						// Where the press landed, not where the pointer is now: a Selectable
						// fires on release, and a press that started on the arrow is what the
						// reader meant even if the pointer drifted before they let go. Hover
						// is checked too, so an activation that did not come from the mouse
						// — which leaves MouseClickedPos wherever it last was — falls to
						// selecting, the harmless of the two.
						const bool onChevron =
							hovered &&
							ImGui::GetIO().MouseClickedPos[0].x < rowStartX + chevronZone;

						if (onChevron)
						{
							// The arrow does this and nothing else: opening a commit's file
							// list and reading its message are separate questions, and a
							// reader who wanted the other one can ask for it without undoing
							// this.
							ToggleExpanded(commit.Id);
						}
						else
						{
							m_SelectedCommit = commit.Id;

							// The file the pane was showing belongs to whichever commit was
							// clicked last time. Leaving it standing would put a diff from
							// one commit under the row of another, and the reader asked
							// about this one.
							m_SelectedFile.reset();
						}
					}
				}
				else
				{
					const std::shared_ptr<const Core::Diff> diff = diffOf(commit.Id);

					// The row layout was built from this same diffOf a moment ago; a file
					// row only fails to resolve if the cache evicted the entry in between,
					// which a fixed-size cache can do.
					const Core::FileDiff* file =
						(diff && row.File < diff->Files.size()) ? &diff->Files[row.File] : nullptr;

					ImGui::Indent(ImGui::GetFrameHeight());

					if (file != nullptr)
					{
						const std::filesystem::path& path = ShownPath(*file);

						const bool selected = m_SelectedFile.has_value() &&
											  m_SelectedFile->Commit == commit.Id &&
											  m_SelectedFile->Path == path;

						// The letter carries the colour the editor's gutter gives that same
						// change, so a glance down the column says what happened without
						// reading a letter per row.
						const std::string letter(1, ChangeLetter(file->Change));
						const std::string rest = std::format("  {}", path.string());

						if (MarkedSelectable("##file", selected, letter,
											 ChangeColor(file->Change, palette), rest,
											 pathHighlight))
						{
							m_SelectedFile = FileSelection{commit.Id, path};
						}
					}
					else
					{
						// Drawn disabled but still one row tall: a row that drew nothing
						// would consume no height, shifting every row below it and
						// desynchronising the graph from the list for this frame.
						ImGui::BeginDisabled();
						ImGui::Selectable("…", false);
						ImGui::EndDisabled();
					}

					ImGui::Unindent(ImGui::GetFrameHeight());
				}

				ImGui::Unindent(graphWidth);
				ImGui::PopID();
			}
		}

		// Asking is what makes the difference between "this is all of it" and "there is
		// more, we just stopped looking" — the page says which, and a reader at the bottom
		// of an incomplete page wants the rest. Armed only once per page: the count is the
		// thing that actually changes when a request is answered, so latching on it (rather
		// than merely consuming the flag on read) stops the widget re-arming every frame the
		// reader happens to stay at the bottom while the bigger page is still being fetched.
		const std::ptrdiff_t commitCount = static_cast<std::ptrdiff_t>(page.Commits.size());

		if (!page.Complete && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() &&
			commitCount != m_LoadMoreRequestedAtCount)
		{
			m_LoadMoreRequested = true;
			m_LoadMoreRequestedAtCount = commitCount;
		}

		ImGui::SetCursorScreenPos(listOrigin);

		CommitGraphView{}.Render(graph, palette, metrics, rows.RowOfCommit, firstDrawn, lastDrawn);
	}
}
