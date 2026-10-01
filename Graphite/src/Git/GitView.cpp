#include "Git/GitView.h"

#include "Core/ImGui/EmptyState.h"

#include "Editor/Palettes.h"
#include "Git/GitPalette.h"
#include "Git/HistoryFilter.h"
#include "Git/HistoryPlaceholder.h"
#include "Git/InitRepositoryPrompt.h"
#include "UI/HighlightedRow.h"
#include "UI/SplitResults.h"
#include "Workspace/Workspace.h"

#include <IconsMaterialDesignIcons.h>

#include <imgui.h>

#include <chrono>
#include <cstddef>
#include <exception>
#include <format>
#include <functional>
#include <unordered_set>

namespace Graphite {

	namespace {

		// The reader's own offset from UTC, for the absolute timestamps in the details pane.
		// Resolved on every call rather than cached: it changes at a daylight-saving
		// boundary, and a panel that keeps drawing after one would otherwise be an hour out
		// until the application restarts.
		//
		// Zero when the time zone database is not available — which happens on a Windows
		// install without the ICU data chrono reads — so a timestamp falls back to UTC
		// rather than the pane failing to draw.
		std::chrono::minutes LocalOffset()
		{
			try
			{
				const auto now = std::chrono::system_clock::now();

				return std::chrono::floor<std::chrono::minutes>(
					std::chrono::current_zone()->get_info(now).offset);
			}
			catch (const std::exception&)
			{
				return std::chrono::minutes{0};
			}
		}
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
		: Core::Panel("git", "Git", "git", ICON_MDI_SOURCE_BRANCH, false), m_Model(model), m_Scheme(scheme),
		  m_DiffWindows(diffWindows)
	{
	}

	void GitView::SubscribeTo(Core::EventBus& events)
	{
		// GitModel::Clear() drops the log on both events, and nothing else re-requests it,
		// so without this a folder opened after another one leaves the history column
		// loading forever - RenderCommits' own latch would stay set from the first folder.
		const auto forget = [this] {
			m_LogRequested = false;
			m_Commits.Reset();

			// The boxes are cleared along with everything else they select from: a path
			// typed against the previous repository names a file this one may not have, and
			// leaving the text standing would show an empty history with no visible reason.
			m_FilterBuffer = {};
			m_FilterPathBuffer = {};
			m_RefFilterBuffer = {};
			m_Filter.Reset();

			// A ref chosen in the previous repository names a branch this one may not have, and
			// asking for it shows an error instead of this folder's history.
			m_SelectedRef.reset();

			m_PageWhenAsked.reset();
			m_LastSeenPage.reset();
			m_Searching = false;
		};

		events.Subscribe<WorkspaceOpenedEvent>([forget](const WorkspaceOpenedEvent&) { forget(); });
		events.Subscribe<WorkspaceClosedEvent>([forget](const WorkspaceClosedEvent&) { forget(); });
	}

	void GitView::OnImGuiRender()
	{
		UpdateForNewPage();

		// The whole panel rather than the history column: with no repository there are no
		// refs to list either, and a prompt squeezed beside an empty column reads as broken.
		if (IsMissingRepository(m_Model.GetStatus() != nullptr, m_Model.GetStatusError()))
		{
			DrawInitRepositoryPrompt(m_Model);
			return;
		}

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

			RenderFilterBar();

			// Above the split rather than inside the list: it describes the page as a whole,
			// and a line that scrolls away with the first few rows describes it only until
			// the reader starts reading.
			if (const std::shared_ptr<const Core::LogPage> page = m_Model.GetLog();
				page && m_Filter.Get() != HistoryFilter{})
			{
				RenderFilterSummary(*page);
			}

			const std::function<void()> drawList = [this, &palette] { RenderCommits(palette); };
			const std::function<void()> drawDiff = [this, &palette] { RenderPreview(palette); };

			// Either selection is worth opening the pane for: a commit with no file picked
			// still has a message to read, which is the whole point of selecting it.
			const bool hasSelection = m_Commits.GetSelectedFile().has_value() ||
									  m_Commits.GetSelectedCommit().has_value();

			DrawSplitResults(SplitResultsInput{.HasSelection = hasSelection,
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
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##refFilter", ICON_MDI_MAGNIFY "  Filter branches",
								 m_RefFilterBuffer.data(), m_RefFilterBuffer.size());

		const std::string refFilter(m_RefFilterBuffer.data());

		const auto group = [&](const char* heading, Core::RefKind kind) {
			ImGui::SeparatorText(heading);

			for (const Core::Branch& branch : branches)
			{
				if (branch.Kind != kind)
				{
					continue;
				}

				// Matched against the full name, so "origin" narrows to one remote's
				// branches even though the label shows only the short name.
				if (!MatchesRefFilter(branch.FullName, refFilter))
				{
					continue;
				}

				const bool selected = m_SelectedRef.has_value() && *m_SelectedRef == branch.FullName;

				// Marked the same way the commit rows are, so the two columns of this panel
				// answer a filter in one language.
				if (HighlightedSelectable("##ref", selected, RefLabel(branch), refFilter))
				{
					m_SelectedRef = branch.FullName;

					// Through RequestLog, not a request built here: picking a branch while a
					// filter is on means "this branch, still filtered", and a request built
					// from the ref alone would clear the boxes' effect while leaving their
					// text on screen.
					RequestLog(std::nullopt);
				}
			}
		};

		group("Local", Core::RefKind::Local);
		group("Remote", Core::RefKind::Remote);
	}

	void GitView::RequestLog(std::optional<std::size_t> maxCount)
	{
		m_Model.RequestLog(BuildLogRequest(m_SelectedRef, m_Filter.Get(), maxCount));

		// Latched here rather than at each call site, so the first-page latch cannot fire a
		// second query over the top of one this just sent. Accept does the same job for the
		// settling filter: this query already carries whatever is typed, so the wait must
		// not run out afterwards and send it a second time.
		m_LogRequested = true;
		m_Filter.Accept();

		m_PageWhenAsked = m_Model.GetLog();
		m_Searching = true;
	}

	void GitView::UpdateForNewPage()
	{
		const std::shared_ptr<const Core::LogPage> page = m_Model.GetLog();

		if (page != m_LastSeenPage)
		{
			m_LastSeenPage = page;

			// A narrowed filter drops commits, and the preview may have been showing a file
			// from one of them — a pane answering a question nothing on screen asks any
			// more. A page that still lists the commit leaves the reader where they were.
			if (page)
			{
				m_Commits.KeepSelectionWithin(*page);
			}
			else
			{
				m_Commits.ClearSelection();
			}
		}

		if (!m_Searching)
		{
			return;
		}

		// A different page than the one that was on screen when the query went out is that
		// query's answer. An error ends the wait too: a failed query leaves the old page
		// standing, and a "Searching…" that never finishes would be the only thing the
		// reader is told about it.
		if (page != m_PageWhenAsked || m_Model.GetLastError().has_value())
		{
			m_Searching = false;
			m_PageWhenAsked.reset();
		}
	}

	void GitView::RenderFilterBar()
	{
		// Wide enough for the example in the box's own hint: a hint clipped halfway through
		// teaches the shape of the path less well than no hint at all.
		const float pathWidth = ImGui::GetFontSize() * 14.0f;

		// The text takes what the path leaves, rather than each taking half: a summary is
		// longer than a path most of the time, and a reader who wants the path box wide can
		// drag the column.
		ImGui::SetNextItemWidth(-(pathWidth + ImGui::GetStyle().ItemSpacing.x));
		ImGui::InputTextWithHint("##commitFilter",
								 ICON_MDI_MAGNIFY "  Message, author or hash",
								 m_FilterBuffer.data(), m_FilterBuffer.size());

		ImGui::SameLine();
		ImGui::SetNextItemWidth(pathWidth);
		// Two examples rather than one word: the box takes a file name or a pattern, and
		// naming both is what says the second is allowed. Neither example names a file that
		// has to exist, which the previous hint's full path did — and did not, in most
		// repositories this ever opens.
		ImGui::InputTextWithHint("##commitPath", ICON_MDI_FILE_OUTLINE "  Main.cpp or *.cpp",
								 m_FilterPathBuffer.data(), m_FilterPathBuffer.size());

		m_Filter.Set(HistoryFilter{std::string(m_FilterBuffer.data()),
								   std::string(m_FilterPathBuffer.data())});

		if (m_Filter.Advance(ImGui::GetIO().DeltaTime))
		{
			// Back to the default page size on every change: a reader who loaded four pages
			// of unfiltered history and then typed a word is asking a new question, and
			// answering it at the old size would walk far more than they asked for.
			RequestLog(std::nullopt);
		}
	}

	void GitView::RenderFilterSummary(const Core::LogPage& page)
	{
		if (m_Searching)
		{
			ImGui::TextDisabled("Searching…");
			return;
		}

		const std::size_t count = page.Commits.size();

		if (page.Complete)
		{
			ImGui::TextDisabled("%zu matching %s", count, count == 1 ? "commit" : "commits");
			return;
		}

		// The distinction the page's own Complete flag exists for: a filtered walk that hit
		// its ceiling found these and stopped looking, which is not the same as these being
		// all there are.
		ImGui::TextDisabled("%zu matching %s so far — the search stopped before the end of "
							"the history", count, count == 1 ? "commit" : "commits");
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
			RequestLog(std::nullopt);
		}

		// A different page than the one that was on screen when the query went out is that
		// query's answer. Cleared on an error too: a failed query leaves the old page
		// standing, and a "Searching…" that never ends would be the only thing the reader
		// is told about it.
		// Called as DrawSplitResults's DrawResults callback, inside the child it opened sized
		// to the split the user dragged. The list's table fills that child and scrolls itself,
		// so its header can stay put while the rows move under it.
		if (!page)
		{
			Core::EmptyState(HistoryPlaceholder(m_Model.GetStatus() != nullptr, m_Model.GetStatusError(),
												m_Model.GetLogError()));
		}
		else
		{
			const Core::CommitGraph graph = Core::LayoutCommits(page->Commits);

			const auto diffOf = [this](const Core::Oid& commit) {
				return m_Model.GetDiffFor(commit);
			};

			m_Commits.Render(*page, graph, palette, LocalOffset(), diffOf, m_Filter.Get().Text,
							 m_Filter.Get().Path);

			// Driven from here, not from inside the list: the widget must not know the
			// model. Asking for a diff already held, in flight, or previously failed costs
			// nothing on the model side, which is what makes it safe to ask about every
			// such commit every frame.
			//
			// Two things want a commit's diff, and either one on its own is reason to ask
			// for it: an expanded row, which lists the files out of it, and the selected
			// commit, whose details pane counts its changed lines. They are separate
			// gestures now that the arrow is what expands, so a selected commit is
			// routinely not expanded and would otherwise never have a diff to count.
			std::unordered_set<Core::Oid> wanted;

			const std::optional<Core::Oid> selectedCommit = m_Commits.GetSelectedCommit();

			for (const Core::CommitSummary& commit : page->Commits)
			{
				if (!m_Commits.IsExpanded(commit.Id) && selectedCommit != commit.Id)
				{
					continue;
				}

				wanted.insert(commit.Id);

				if (!m_Model.GetDiffFor(commit.Id))
				{
					m_Model.RequestDiffFor(commit.Id);
				}
			}

			// A commit whose diff was wanted last frame and is not now was just collapsed or
			// deselected. Only a failed entry is actually forgotten here - the model decides
			// that, since it is the only thing that knows an entry failed - which is what
			// gives a failure a way back: expanding or selecting the row again is the same
			// gesture that asks for the diff in the first place. A successful diff stays
			// cached, so collapsing and re-expanding a row does not re-query git.
			for (const Core::Oid& commit : m_PreviouslyWanted)
			{
				if (!wanted.contains(commit))
				{
					m_Model.ForgetFailedDiffFor(commit);
				}
			}

			m_PreviouslyWanted = std::move(wanted);

			if (m_Commits.TakeLoadMoreRequest())
			{
				// Ask for a page twice the size rather than appending: the model holds one
				// snapshot per query, and merging pages would need a second concept of
				// identity for "the same log, more of it".
				//
				// Twice what is on screen, which under a filter is twice the matches rather
				// than twice the commits walked — the walk goes as deep as it must to find
				// them, bounded by LogRequest::MaxWalk and nothing else.
				RequestLog(page->Commits.size() * 2);
			}
		}
	}

	void GitView::RenderPreview(const GitPalette& palette)
	{
		if (m_Commits.GetSelectedFile().has_value())
		{
			RenderDiff(palette);
			return;
		}

		const std::optional<Core::Oid> commit = m_Commits.GetSelectedCommit();
		const std::shared_ptr<const Core::LogPage> page = m_Model.GetLog();

		if (!commit.has_value() || !page)
		{
			RenderDiff(palette);
			return;
		}

		for (const Core::CommitSummary& summary : page->Commits)
		{
			if (summary.Id == *commit)
			{
				RenderDetails(summary, palette);
				return;
			}
		}

		// The selection outlived the page that carried it, which UpdateForNewPage exists to
		// prevent — but it runs at the top of the frame and this reads the model again, so
		// the honest fallback is the empty pane rather than a stale commit kept for the
		// purpose.
		RenderDiff(palette);
	}

	void GitView::RenderDetails(const Core::CommitSummary& commit, const GitPalette& palette)
	{
		// The counts come from the diff the model already holds; a commit whose diff has not
		// landed yet draws the line dimmed rather than asking for one here. RenderCommits is
		// what asks, for the selected commit as well as the expanded ones, so this pane never
		// waits on a query nobody sent.
		std::optional<DiffStats> stats;

		if (const std::shared_ptr<const Core::Diff> diff = m_Model.GetDiffFor(commit.Id))
		{
			stats = CountChanges(*diff);
		}

		if (Graphite::RenderCommitDetails(commit, stats, LocalOffset(), palette))
		{
			// Only the commit: a file selected under some other row keeps its diff, which is
			// what the pane falls back to showing.
			m_Commits.ClearCommitSelection();
		}
	}

	void GitView::RenderDiff(const GitPalette& palette)
	{
		const std::optional<FileSelection> selected = m_Commits.GetSelectedFile();

		if (!selected.has_value())
		{
			// Cleared rather than left standing: without a selection there is nothing a
			// button drawn later this frame could open, and a stale callback captured from
			// whatever was selected last frame is a thing the next reader would have to
			// reason is unreachable rather than something that is simply not there.
			m_Diff.OnOpenAsTab = nullptr;
			m_Diff.OnClose = nullptr;
			m_Diff.OnOpenFile = nullptr;
			Core::EmptyState("Select a commit or a file to see what changed.");
			return;
		}

		// Reassigned every frame rather than bound once in the constructor: the selection it
		// captures changes with whichever file is selected now, and m_Diff is the one bottom
		// pane shared across every selection. Set as soon as the selection is known, ahead of
		// the two returns below, so OnOpenAsTab is never left holding a previous frame's
		// selection while this one's diff is still loading or excludes the file.
		const OpenDiff selection{
			.Commit = selected->Commit, .Target = Core::DiffTarget::CommitToParent, .Path = selected->Path};

		m_Diff.OnOpenAsTab = [this, selection] { m_DiffWindows.Open(selection); };

		// Cleared alongside OnOpenAsTab above when nothing is selected, and reassigned here
		// every frame for the same reason: this pane is shared across every selection.
		m_Diff.OnClose = [this] { m_Commits.ClearSelection(); };

		m_Diff.OnOpenFile = m_DiffWindows.OpenFileIn(m_Model);

		const std::shared_ptr<const Core::Diff> diff = m_Model.GetDiffFor(selected->Commit);

		if (!diff)
		{
			Core::EmptyState("Loading…");
			return;
		}

		const std::optional<std::size_t> index = IndexOfFile(*diff, selected->Path);

		if (!index.has_value())
		{
			Core::EmptyState("That file is not in this diff.");
			return;
		}

		const Core::FileDiff& file = diff->Files[*index];

		// Every rule about which path keys the texts, when the tokens are still valid and when
		// a failed entry is forgiven lives in Update, shared with the detached tabs - the same
		// steps written out twice were wrong the same way in both copies.
		m_Texts.Update(m_Model, selected->Commit, file);

		const SyntaxPalette syntax = ReadSyntaxPalette(m_Scheme.Get());
		m_Diff.Render(file, palette, syntax, m_Texts.Texts, m_Texts.Tokens);
	}
}
