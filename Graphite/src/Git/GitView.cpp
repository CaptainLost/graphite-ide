#include "Git/GitView.h"

#include "Git/GitPalette.h"
#include "UI/SplitResults.h"
#include "Workspace/Workspace.h"

#include <imgui.h>

#include <format>
#include <functional>
#include <unordered_set>

namespace Graphite {

	std::optional<std::size_t> IndexOfFile(const Core::Diff& diff,
										   const std::filesystem::path& path)
	{
		for (std::size_t index = 0; index < diff.Files.size(); ++index)
		{
			const Core::FileDiff& file = diff.Files[index];

			if (file.NewPath == path || file.OldPath == path)
			{
				return index;
			}
		}

		return std::nullopt;
	}

	std::string RefLabel(const Core::Branch& branch)
	{
		std::string label = branch.IsHead ? "* " + branch.Name : branch.Name;

		// Ahead and Behind mean nothing unless the distance was measured — zero and zero
		// also spells "in sync", so a caller that skips this check states a distance
		// nobody computed. Core::Branch documents the same rule at its own definition.
		if (branch.DistanceKnown && (branch.Ahead > 0 || branch.Behind > 0))
		{
			label += std::format("  {}/{}", branch.Ahead, branch.Behind);
		}

		return label;
	}

	GitView::GitView(GitModel& model, const ActiveScheme& scheme, DiffWindows& diffWindows)
		: Core::Panel("git", "Git", false), m_Model(model), m_Scheme(scheme),
		  m_DiffWindows(diffWindows)
	{
	}

	void GitView::SubscribeTo(Core::EventBus& events)
	{
		// GitModel::Clear() drops the log on both events, and nothing else re-requests it,
		// so without this a folder opened after another one leaves the history column
		// loading forever - RenderCommits' own latch would stay set from the first folder.
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) {
			m_LogRequested = false;
			m_Commits.Reset();
		});

		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) {
			m_LogRequested = false;
			m_Commits.Reset();
		});
	}

	void GitView::OnImGuiRender()
	{
		const GitPalette palette = ReadGitPalette(m_Scheme.Get());

		const std::shared_ptr<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

		if (ImGui::BeginTable("git", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("refs", ImGuiTableColumnFlags_WidthFixed,
									ImGui::GetFontSize() * 10.0f);
			ImGui::TableSetupColumn("history", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			RenderRefs(branches ? *branches : std::vector<Core::Branch>{});

			ImGui::TableSetColumnIndex(1);

			const std::function<void()> drawList = [this, &palette] { RenderCommits(palette); };
			const std::function<void()> drawDiff = [this, &palette] { RenderDiff(palette); };

			DrawSplitResults(SplitResultsInput{.HasSelection = m_Commits.GetSelectedFile().has_value(),
											   .ResultsHeight = m_ListHeight,
											   .DrawResults = drawList,
											   .DrawPreview = drawDiff,
											   .ResultsChildId = "##gitCommits",
											   .PreviewChildId = "##gitDiff",
											   .SplitterId = "##gitSplit"});

			ImGui::EndTable();
		}

		if (const std::optional<Core::Error>& error = m_Model.GetLastError(); error.has_value())
		{
			// A line, not a modal: a failed refresh is worth saying and not worth
			// interrupting for.
			ImGui::TextUnformatted(error->Message.c_str());
		}
	}

	void GitView::RenderRefs(const std::vector<Core::Branch>& branches)
	{
		const auto group = [&](const char* heading, Core::RefKind kind) {
			ImGui::SeparatorText(heading);

			for (const Core::Branch& branch : branches)
			{
				if (branch.Kind != kind)
				{
					continue;
				}

				const bool selected = m_SelectedRef.has_value() && *m_SelectedRef == branch.FullName;

				if (ImGui::Selectable(RefLabel(branch).c_str(), selected))
				{
					m_SelectedRef = branch.FullName;

					Core::LogRequest request;
					request.Ref = branch.FullName;
					m_Model.RequestLog(request);
				}
			}
		};

		group("Local", Core::RefKind::Local);
		group("Remote", Core::RefKind::Remote);
	}

	void GitView::RenderCommits(const GitPalette& palette)
	{
		const std::shared_ptr<const Core::LogPage> page = m_Model.GetLog();

		// Nothing calls RequestLog until a ref is clicked or a page runs out, so without
		// this the history column stays empty until the user happens to click a branch.
		// Guarded on m_LogRequested, not merely on page being null, so a first query still
		// in flight is not re-asked every frame it takes to come back.
		if (!page && !m_LogRequested)
		{
			m_LogRequested = true;
			m_Model.RequestLog({});
		}

		// No child of its own: this is called as DrawSplitResults's DrawResults callback, which
		// already opened one sized to the split the user dragged. Opening a second here would
		// nest a child inside a child, each with its own scrollbar and its own scroll position —
		// and CommitListView::Render's load-more latch reads ImGui::GetScrollY() against
		// whichever window is current when it runs, so it has to be this one.
		if (!page)
		{
			ImGui::TextUnformatted("Loading history…");
		}
		else
		{
			const Core::CommitGraph graph = Core::LayoutCommits(page->Commits);

			const auto diffOf = [this](const Core::Oid& commit) {
				return m_Model.GetDiffFor(commit);
			};

			m_Commits.Render(*page, graph, palette, diffOf);

			// Driven from here, not from inside the list: the widget must not know the
			// model. Asking for a diff already held, in flight, or previously failed costs
			// nothing on the model side, which is what makes it safe to ask about every
			// expanded commit every frame.
			std::unordered_set<Core::Oid> currentlyExpanded;

			for (const Core::CommitSummary& commit : page->Commits)
			{
				if (!m_Commits.IsExpanded(commit.Id))
				{
					continue;
				}

				currentlyExpanded.insert(commit.Id);

				if (!m_Model.GetDiffFor(commit.Id))
				{
					m_Model.RequestDiffFor(commit.Id);
				}
			}

			// A commit expanded last frame but not this one was just collapsed. Only a failed
			// entry is actually forgotten here - the model decides that, since it is the only
			// thing that knows an entry failed - which is what gives a failure a way back:
			// expanding the row again is the same gesture that asks for the diff in the first
			// place. A successful diff stays cached, so collapsing and re-expanding a row does
			// not re-query git.
			for (const Core::Oid& commit : m_PreviouslyExpanded)
			{
				if (!currentlyExpanded.contains(commit))
				{
					m_Model.ForgetFailedDiffFor(commit);
				}
			}

			m_PreviouslyExpanded = std::move(currentlyExpanded);

			if (m_Commits.TakeLoadMoreRequest())
			{
				// Ask for a page twice the size rather than appending: the model holds one
				// snapshot per query, and merging pages would need a second concept of
				// identity for "the same log, more of it".
				Core::LogRequest request;
				request.Ref = m_SelectedRef;
				request.MaxCount = page->Commits.size() * 2;
				m_Model.RequestLog(request);
			}
		}
	}

	void GitView::RenderDiff(const GitPalette& palette)
	{
		const std::optional<FileSelection> selected = m_Commits.GetSelectedFile();

		if (!selected.has_value())
		{
			ImGui::TextUnformatted("Select a file to see its changes.");
			return;
		}

		const std::shared_ptr<const Core::Diff> diff = m_Model.GetDiffFor(selected->Commit);

		if (!diff)
		{
			ImGui::TextUnformatted("Loading…");
			return;
		}

		const std::optional<std::size_t> index = IndexOfFile(*diff, selected->Path);

		if (!index.has_value())
		{
			ImGui::TextUnformatted("That file is not in this diff.");
			return;
		}

		// Reassigned every frame rather than bound once in the constructor: the selection it
		// captures changes with whichever file is selected now, and m_Diff is the one bottom
		// pane shared across every selection.
		const OpenDiff selection{selected->Commit, selected->Path};

		m_Diff.OnOpenAsTab = [this, selection] { m_DiffWindows.Open(selection); };

		m_Diff.Render(diff->Files[*index], palette);
	}
}
