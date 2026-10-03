#include "Git/GitView.h"

#include "Core/Base/Base.h"

#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
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

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <format>
#include <functional>
#include <unordered_set>

namespace Graphite {

	namespace {

		// `path` relative to `root`, compared as Core compares every path: the editor and git can
		// spell the same directory differently on Windows - a drive letter, for one. A path outside
		// the root climbs out with "..", as lexically_relative spells it.
		std::filesystem::path RelativeTo(const std::filesystem::path& path, const std::filesystem::path& root)
		{
			return Core::RelativeTo(path, root).value_or(path.lexically_relative(root));
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

	GitView::GitView(GitModel& model, const ActiveScheme& scheme, DiffWindows& diffWindows, ModalGate& modals,
					 EditorBuffers& buffers)
		: Core::Panel("git", "Git", "git", ICON_MDI_SOURCE_BRANCH, false), m_Model(model), m_Scheme(scheme),
		  m_DiffWindows(diffWindows), m_Buffers(buffers), m_CheckoutQuestion(modals, "###GitCheckout"),
		  m_NewBranchQuestion(modals, "###GitNewBranch"), m_RenameBranchQuestion(modals, "###GitRenameBranch"),
		  m_DeleteBranchQuestion(modals, "###GitDeleteBranch"), m_DropStashQuestion(modals, "###GitDropStash")
	{
	}

	void GitView::OnUpdate(Core::Timestep)
	{
		// Begin is what hands the gate back, and it runs from OnImGuiRender, which the panel
		// manager skips for a panel that is hidden or behind another tab - see
		// ChangesPanel::OnUpdate for why dropping the question is the lesser harm.
		if (!HasBeenDrawn())
		{
			WithdrawQuestions();
		}

		FollowSelectedRef();

		if (std::optional<CheckoutPlanAnswer> answer = m_Model.TakeCheckoutPlan())
		{
			ApplyCheckoutPlan(std::move(*answer));
		}

		if (std::optional<DeleteCheck> check = m_Model.TakeDeleteCheck())
		{
			// Only the check this panel is waiting for: anything else was asked for a branch
			// the reader has since moved on from.
			if (m_PendingDelete == check->Branch)
			{
				m_PendingDelete.reset();

				if (check->Failure.has_value())
				{
					m_ActionError = "Could not delete " + check->Branch + ": " + check->Failure->Message;
				}
				else
				{
					AskOnce(m_DeleteBranchQuestion,
							DeleteBranchPayload{.Branch = check->Branch, .Unmerged = check->Unmerged});
				}
			}
		}

		// Drained every frame, drawn or not, for the reason ChangesPanel drains TakeDiscarded
		// here: an expectation registered long after the write is one for the wrong change.
		if (std::vector<std::filesystem::path> changed = m_Model.TakeCheckedOut(); !changed.empty())
		{
			const std::filesystem::path& root = m_Model.GetWorkingDirectory();

			// Relative paths joined onto nothing would name the wrong files.
			if (root.empty())
			{
				return;
			}

			std::vector<std::filesystem::path> absolute;
			absolute.reserve(changed.size());

			for (const std::filesystem::path& path : changed)
			{
				absolute.push_back(root / path);
			}

			// Checked against the documents as they are now, not as the question saw them:
			// an edit made while the checkout ran must not be reloaded away unasked.
			m_Buffers.ExpectExternalWrite(SilentReloads(absolute, m_Buffers.ModifiedUnder(root)));
		}
	}

	void GitView::FollowSelectedRef()
	{
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

		if (branches == m_LastSeenBranches)
		{
			return;
		}

		m_LastSeenBranches = branches;

		if (!branches || !m_SelectedRef.has_value())
		{
			return;
		}

		const auto exists = [&branches](const std::string& fullName) {
			return std::any_of(branches->begin(), branches->end(),
							   [&fullName](const Core::Branch& branch) { return branch.FullName == fullName; });
		};

		if (exists(*m_SelectedRef))
		{
			return;
		}

		if (m_PendingRename.has_value() && m_PendingRename->first == *m_SelectedRef &&
			exists(m_PendingRename->second))
		{
			m_SelectedRef = m_PendingRename->second;
		}
		else
		{
			m_SelectedRef.reset();
		}

		m_PendingRename.reset();
		RequestLog(std::nullopt);
	}

	void GitView::WithdrawQuestions()
	{
		// Only a question actually asked, as ChangesPanel does: nothing pending, nothing to
		// hand back.
		if (m_CheckoutQuestion.IsAsked())
		{
			m_CheckoutQuestion.Withdraw();
		}

		if (m_NewBranchQuestion.IsAsked())
		{
			m_NewBranchQuestion.Withdraw();
		}

		if (m_RenameBranchQuestion.IsAsked())
		{
			m_RenameBranchQuestion.Withdraw();
		}

		if (m_DeleteBranchQuestion.IsAsked())
		{
			m_DeleteBranchQuestion.Withdraw();
		}

		if (m_DropStashQuestion.IsAsked())
		{
			m_DropStashQuestion.Withdraw();
		}

		// The dialogs' own edits go with them: a question asked again before the next Begin
		// would otherwise open on the withdrawn one's checkbox, text and failure line.
		m_CheckoutDialogOpen = false;
		m_IncludeUntracked = false;
		m_StashSaveFailed = false;
		m_BranchNameFor = nullptr;
	}

	void GitView::BeginCheckout(const Core::CheckoutTarget& target)
	{
		m_ActionError.reset();
		m_PendingCheckout = target;
		m_Model.RequestCheckoutPlan(target);
	}

	void GitView::ApplyCheckoutPlan(CheckoutPlanAnswer answer)
	{
		if (m_PendingCheckout != answer.Target)
		{
			return;
		}

		m_PendingCheckout.reset();

		const std::string label = CheckoutTargetLabel(answer.Target);

		if (answer.Failure.has_value() || !answer.Plan.has_value())
		{
			m_ActionError = "Could not check out " + label + ": " +
							(answer.Failure.has_value() ? answer.Failure->Message : std::string("no plan"));
			return;
		}

		const std::filesystem::path& root = m_Model.GetWorkingDirectory();
		std::vector<std::filesystem::path> unsaved =
			root.empty() ? std::vector<std::filesystem::path>{} : m_Buffers.ModifiedUnder(root);

		std::vector<std::filesystem::path> unsavedRelative;
		unsavedRelative.reserve(unsaved.size());

		for (const std::filesystem::path& path : unsaved)
		{
			unsavedRelative.push_back(RelativeTo(path, root));
		}

		if (!NeedsCheckoutDialog(*answer.Plan, unsavedRelative))
		{
			m_Model.RequestCheckout(CheckoutRequest{.Target = answer.Target});
			return;
		}

		// No status yet lists no changes of git's; the unsaved documents are still asked about.
		const Core::Ref<const Core::WorkingTreeStatus> status = m_Model.GetStatus();

		CheckoutQuestion question = BuildCheckoutQuestion(
			label, *answer.Plan, status ? *status : Core::WorkingTreeStatus{}, unsavedRelative);

		const bool forced = question.UntrackedForced;

		AskOnce(m_CheckoutQuestion, CheckoutQuestionPayload{.Target = std::move(answer.Target),
															.Question = std::move(question),
															.UnsavedAbsolute = std::move(unsaved),
															.IncludeUntracked = forced});
	}

	void GitView::BeginNewBranch(std::string from, const Core::Oid& at, std::string_view suggested)
	{
		BranchNamePayload payload{.Mode = BranchNameMode::New, .From = std::move(from), .At = at};

		const std::size_t length = std::min(suggested.size(), payload.Buffer.size() - 1);
		std::copy_n(suggested.begin(), length, payload.Buffer.begin());

		AskOnce(m_NewBranchQuestion, std::move(payload));
	}

	void GitView::BeginRename(const std::string& branch)
	{
		BranchNamePayload payload{.Mode = BranchNameMode::Rename, .From = branch};

		// Starts from the old name: a rename is usually a small edit to it.
		const std::size_t length = std::min(branch.size(), payload.Buffer.size() - 1);
		std::copy_n(branch.begin(), length, payload.Buffer.begin());

		AskOnce(m_RenameBranchQuestion, std::move(payload));
	}

	void GitView::ApplyCommitAction()
	{
		const std::optional<CommitAction> action = m_Commits.TakeCommitAction();

		if (!action.has_value())
		{
			return;
		}

		switch (action->Action)
		{
		case CommitAction::Kind::Checkout:
			BeginCheckout(Core::CheckoutTarget{.Kind = Core::CheckoutKind::Commit, .Commit = action->Commit});
			break;
		case CommitAction::Kind::NewBranch:
			BeginNewBranch(action->Commit.ShortHex(), action->Commit);
			break;
		}
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
			m_SelectedStash.reset();
			m_LastSeenBranches.reset();
			m_PendingRename.reset();

			m_PageWhenAsked.reset();
			m_LastSeenPage.reset();
			m_Searching = false;

			// Every question and every answer awaited was about the repository that is gone.
			// Withdrawn here, outside any popup, which is the only place Withdraw is valid.
			WithdrawQuestions();
			m_PendingCheckout.reset();
			m_PendingDelete.reset();
			m_ActionError.reset();
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
			// The dialogs below are not drawn on this path, and Begin is what hands the gate
			// back - so a question left standing would hold it for good.
			WithdrawQuestions();
			DrawInitRepositoryPrompt(m_Model);
			return;
		}

		const GitPalette palette = ReadGitPalette(m_Scheme.Get());

		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

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
			if (const Core::Ref<const Core::LogPage> page = m_Model.GetLog();
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

		RenderCheckoutDialog();
		RenderBranchNameDialog(m_NewBranchQuestion);
		RenderBranchNameDialog(m_RenameBranchQuestion);
		RenderDeleteBranchDialog();
		RenderDropStashDialog();
	}

	void GitView::RenderRefs(const std::vector<Core::Branch>& branches)
	{
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##refFilter", ICON_MDI_MAGNIFY "  Filter branches",
								 m_RefFilterBuffer.data(), m_RefFilterBuffer.size());

		const std::string refFilter(m_RefFilterBuffer.data());

		RenderErrors();

		const auto group = [&](const char* heading, Core::RefKind kind, RefSection section) {
			// Matched against the full name, so "origin" narrows to one remote's branches
			// even though the label shows only the short name.
			const auto shown = [&](const Core::Branch& branch) {
				return branch.Kind == kind && MatchesRefFilter(branch.FullName, refFilter);
			};

			// The rows the section would list, filter applied: the count says what folding
			// it hides, not how many branches there are behind the filter.
			const std::size_t count =
				static_cast<std::size_t>(std::ranges::count_if(branches, shown));

			if (!RenderRefSection(heading, count, section))
			{
				return;
			}

			for (const Core::Branch& branch : branches)
			{
				if (!shown(branch))
				{
					continue;
				}

				// One row of this column is selected at a time: a selected stash is what the pane
				// below describes, and a branch lit beside it reads as a second answer to that.
				const bool selected = !m_SelectedStash.has_value() && m_SelectedRef.has_value() &&
									  *m_SelectedRef == branch.FullName;

				// Each row's menu is its own popup.
				ImGui::PushID(branch.FullName.c_str());

				// Grouped so the menu below asks about the whole row: with the filter marked,
				// HighlightedSelectable ends on a piece of its label, and asking that would
				// open the menu only over the text after the match.
				ImGui::BeginGroup();

				// Marked the same way the commit rows are, so the two columns of this panel
				// answer a filter in one language.
				//
				// Cut to the column like the stash rows, so a long name ends in an ellipsis
				// rather than at the column's edge, and is said whole in a tooltip.
				const std::string label = RefLabel(branch);

				bool cut = false;
				const std::string fitted = FitToLine(label, &cut);

				const bool clicked = HighlightedSelectable("##ref", selected, fitted, refFilter);

				ImGui::EndGroup();

				if (cut)
				{
					ImGui::SetItemTooltip("%s", label.c_str());
				}

				RenderRefMenu(branch);

				ImGui::PopID();

				if (clicked)
				{
					m_SelectedRef = branch.FullName;

					// Picking a branch is going back to the history, and a stash is on no
					// branch's page - the rule a new page applies to a selected commit.
					DeselectStash();

					// Through RequestLog, not a request built here: picking a branch while a
					// filter is on means "this branch, still filtered", and a request built
					// from the ref alone would clear the boxes' effect while leaving their
					// text on screen.
					RequestLog(std::nullopt);
				}
			}
		};

		group("Local", Core::RefKind::Local, RefSection::Local);
		group("Remote", Core::RefKind::Remote, RefSection::Remote);

		RenderStashes();
	}

	bool GitView::RenderRefSection(const char* heading, std::size_t count, RefSection section)
	{
		bool& collapsed = m_CollapsedRefSections[static_cast<std::size_t>(section)];

		ImGui::PushID(heading);

		// The heading stays the separator line it was, with a chevron ahead of it, laid over
		// one Selectable as tall as that line so the whole of it folds the section. The
		// separator draws its text inset by its own padding, which the chevron is moved down
		// by to sit on the same baseline.
		const float padding = ImGui::GetStyle().SeparatorTextPadding.y;
		const ImVec2 rowStart = ImGui::GetCursorPos();

		if (ImGui::Selectable("##section", false, ImGuiSelectableFlags_AllowOverlap,
							  ImVec2(0.0f, ImGui::GetTextLineHeight() + padding * 2.0f)))
		{
			collapsed = !collapsed;
		}

		ImGui::SetCursorPos(ImVec2(rowStart.x, rowStart.y + padding));
		ImGui::TextDisabled("%s", collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN);

		// No inset before the heading: the separator's own puts a stub of line between the
		// chevron and the name.
		ImGui::SameLine();
		ImGui::SetCursorPosY(rowStart.y);
		ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(0.0f, padding));
		ImGui::SeparatorText(std::format("{} ({})", heading, count).c_str());
		ImGui::PopStyleVar();

		ImGui::PopID();

		return !collapsed;
	}

	void GitView::RenderRefMenu(const Core::Branch& branch)
	{
		if (!Core::BeginRightClickPopup("ref-menu"))
		{
			return;
		}

		// One write at a time, and the plan and the delete check are writes too.
		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

		if (branch.Kind == Core::RefKind::Local)
		{
			if (ImGui::MenuItem("Checkout", nullptr, false, !branch.IsHead))
			{
				BeginCheckout(Core::CheckoutTarget{.Kind = Core::CheckoutKind::LocalBranch, .Name = branch.Name});
			}

			if (ImGui::MenuItem("New Branch from Here…"))
			{
				BeginNewBranch(branch.Name, branch.Tip);
			}

			if (ImGui::MenuItem("Rename…"))
			{
				BeginRename(branch.Name);
			}

			// Git refuses to delete the branch it is on, so the item says so before it is
			// clicked rather than after.
			if (ImGui::MenuItem("Delete", nullptr, false, !branch.IsHead))
			{
				m_ActionError.reset();
				m_PendingDelete = branch.Name;
				m_Model.RequestDeleteCheck(branch.Name);
			}
		}
		else
		{
			if (ImGui::MenuItem("Checkout"))
			{
				BeginCheckout(Core::CheckoutTarget{.Kind = Core::CheckoutKind::RemoteBranch, .Name = branch.Name});
			}

			if (ImGui::MenuItem("New Branch from Here…"))
			{
				// Named as the remote's branch is, without the remote: what a local branch
				// tracking origin/feature/y is usually called.
				const std::size_t slash = branch.Name.find('/');
				const std::string_view local =
					slash == std::string::npos ? std::string_view{} : std::string_view(branch.Name).substr(slash + 1);

				BeginNewBranch(branch.Name, branch.Tip, local);
			}
		}

		ImGui::EndDisabled();
		ImGui::EndPopup();
	}

	std::string FitToLine(std::string_view text, bool* cut)
	{
		// Guarded because an empty view may carry no pointer at all, and CalcTextSize reads a
		// null end as "up to the terminator".
		const auto widthOf = [](std::string_view piece) {
			return piece.empty() ? 0.0f : ImGui::CalcTextSize(piece.data(), piece.data() + piece.size()).x;
		};

		// Given back the pixel CalcTextSize rounds up by, for the reason the commit list gives.
		const std::optional<std::size_t> at =
			CutToFit(text, ImGui::GetContentRegionAvail().x + 1.0f, widthOf(Ellipsis), widthOf);

		if (cut != nullptr)
		{
			*cut = at.has_value();
		}

		return at.has_value() ? std::string(text.substr(0, *at)) + std::string(Ellipsis) : std::string(text);
	}

	std::chrono::minutes GitView::LocalOffset()
	{
		try
		{
			const auto now = std::chrono::system_clock::now();

			return std::chrono::floor<std::chrono::minutes>(std::chrono::current_zone()->get_info(now).offset);
		}
		catch (const std::exception&)
		{
			return std::chrono::minutes{0};
		}
	}

	void GitView::RenderErrors()
	{
		// Wrapped: the ref column is narrow, and a message cut at its edge says less than one
		// that runs to a second line.
		if (const std::optional<Core::Error>& error = m_Model.GetWriteError(); error.has_value())
		{
			ImGui::PushID("write-error");
			ImGui::TextWrapped("%s", error->Message.c_str());

			if (ImGui::SmallButton("Dismiss"))
			{
				m_Model.DismissWriteError();
			}

			ImGui::PopID();
		}

		if (m_ActionError.has_value())
		{
			ImGui::PushID("action-error");
			ImGui::TextWrapped("%s", m_ActionError->c_str());

			if (ImGui::SmallButton("Dismiss"))
			{
				m_ActionError.reset();
			}

			ImGui::PopID();
		}
	}

	void GitView::RequestLog(std::optional<std::size_t> maxCount)
	{
		m_Model.RequestLog(BuildLogRequest(m_SelectedRef, m_Filter.Get(), maxCount, m_SelectedStash));

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
		const Core::Ref<const Core::LogPage> page = m_Model.GetLog();

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

		if (m_Filter.Advance(Core::Timestep{ImGui::GetIO().DeltaTime}))
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
							"the history",
							count, count == 1 ? "commit" : "commits");
	}

	void GitView::RenderCommits(const GitPalette& palette)
	{
		const Core::Ref<const Core::LogPage> page = m_Model.GetLog();

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

			m_Commits.SetActionsEnabled(!m_Model.IsWriteInFlight());
			m_Commits.Render(*page, graph, palette, LocalOffset(), diffOf, m_Filter.Get().Text,
							 m_Filter.Get().Path);

			ApplyCommitAction();

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
		const Core::Ref<const Core::LogPage> page = m_Model.GetLog();

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

		if (const Core::Ref<const Core::Diff> diff = m_Model.GetDiffFor(commit.Id))
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

		const Core::Ref<const Core::Diff> diff = m_Model.GetDiffFor(selected->Commit);

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
