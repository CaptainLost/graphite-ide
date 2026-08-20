#include "Git/CommitListView.h"

#include "Git/CommitLabels.h"
#include "Git/CommitRows.h"

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

	bool CommitListView::TakeLoadMoreRequest()
	{
		const bool requested = m_LoadMoreRequested;
		m_LoadMoreRequested = false;
		return requested;
	}

	void CommitListView::Render(const Core::LogPage& page, const Core::CommitGraph& graph,
								const GitPalette& palette,
								const std::function<std::shared_ptr<const Core::Diff>(const Core::Oid&)>& diffOf)
	{
		if (page.Commits.empty())
		{
			ImGui::TextUnformatted("No commits yet.");
			return;
		}

		const GraphMetrics metrics = MetricsForCurrentFont();
		const float graphWidth = GraphWidth(metrics, graph.LaneCount);

		if (!ImGui::BeginChild("commits", ImVec2{0.0f, 0.0f}))
		{
			ImGui::EndChild();
			return;
		}

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

					// The arrow lives in the label rather than as its own ArrowButton, so a
					// commit row stays a plain Selectable — see the note above the clipper.
					const std::string label = std::format(
						"{}  {}  {}  {}  {}",
						expanded ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT,
						commit.Id.ShortHex(), AuthorInitials(commit.Author),
						RelativeAge(commit.Author.When, now), commit.Summary);

					if (ImGui::Selectable(label.c_str(), false))
					{
						ToggleExpanded(commit.Id);
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

						const std::string label =
							std::format("{}  {}", ChangeLetter(file->Change), path.string());

						if (ImGui::Selectable(label.c_str(), selected))
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

		ImGui::EndChild();
	}
}
