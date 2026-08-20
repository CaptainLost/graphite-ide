#include "Git/CommitListView.h"

#include "Git/CommitLabels.h"

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

	void CommitListView::SelectFirst(const Core::LogPage& page)
	{
		m_Selected = page.Commits.empty() ? std::optional<Core::Oid>{}
										  : std::optional<Core::Oid>{page.Commits.front().Id};
	}

	bool CommitListView::TakeLoadMoreRequest()
	{
		const bool requested = m_LoadMoreRequested;
		m_LoadMoreRequested = false;
		return requested;
	}

	void CommitListView::Render(const Core::LogPage& page, const Core::CommitGraph& graph,
								const GitPalette& palette)
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

		// The graph is drawn once, under the whole list, from the list's own origin — so
		// the two cannot disagree about where row N is.
		const ImVec2 listOrigin = ImGui::GetCursorScreenPos();

		const auto now = std::chrono::floor<std::chrono::seconds>(
			std::chrono::system_clock::now());

		int firstDrawn = static_cast<int>(page.Commits.size());
		int lastDrawn = 0;

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(page.Commits.size()), metrics.RowHeight);

		while (clipper.Step())
		{
			firstDrawn = clipper.DisplayStart < firstDrawn ? clipper.DisplayStart : firstDrawn;
			lastDrawn = clipper.DisplayEnd > lastDrawn ? clipper.DisplayEnd - 1 : lastDrawn;

			for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
			{
				const Core::CommitSummary& commit = page.Commits[static_cast<std::size_t>(index)];

				ImGui::PushID(index);
				ImGui::Indent(graphWidth);

				const bool selected = m_Selected.has_value() && *m_Selected == commit.Id;

				const std::string label =
					std::format("{}  {}  {}  {}", commit.Id.ShortHex(),
								AuthorInitials(commit.Author), RelativeAge(commit.Author.When, now),
								commit.Summary);

				if (ImGui::Selectable(label.c_str(), selected))
				{
					m_Selected = commit.Id;
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
		CommitGraphView{}.Render(graph, palette, metrics, firstDrawn, lastDrawn);

		ImGui::EndChild();
	}
}
