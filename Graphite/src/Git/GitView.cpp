#include "Git/GitView.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Base.h"
#include "Core/Base/Strings.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/Time/TimeZone.h"

#include "Theme/Palettes.h"
#include "Theme/GitPalette.h"
#include "UI/ClipboardCopy.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"
#include "Git/DefaultBranch.h"
#include "Git/History/HistoryFilter.h"
#include "Git/History/HistoryPlaceholder.h"
#include "Git/InitRepositoryPrompt.h"
#include "UI/SplitResults.h"
#include "UI/ToastEvent.h"
#include "Workspace/Workspace.h"

#include <IconsLucide.h>

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

		constexpr float kRefsColumnWidthInFontSizes = 10.0f;
		constexpr float kPathBoxWidthInFontSizes = 14.0f;
		constexpr int kGitTableColumnCount = 2;
		constexpr int kRefsColumn = 0;
		constexpr int kHistoryColumn = 1;

		std::filesystem::path RelativeOrClimbing(const std::filesystem::path& path, const std::filesystem::path& root)
		{
			return Core::RelativeTo(path, root).value_or(path.lexically_relative(root));
		}

		void ShowFailure(Core::EventBus& events, std::string title, const std::string& message,
						 const std::filesystem::path& root)
		{
			events.Publish(FolderToast(Core::ToastKind::Error, std::move(title), message, root));
		}
	}

	std::string RefLabel(const Core::Branch& branch)
	{
		std::string label = branch.IsHead ? "* " + branch.Name : branch.Name;

		if (!branch.DistanceKnown)
		{
			return label;
		}

		if (branch.Ahead > 0)
		{
			label += std::format(" {}{}", ICON_LC_ARROW_UP, branch.Ahead);
		}

		if (branch.Behind > 0)
		{
			label += std::format(" {}{}", ICON_LC_ARROW_DOWN, branch.Behind);
		}

		return label;
	}

	GitView::GitView(GitModel& model, const ActiveScheme& scheme, EditorSettings& editorSettings,
					 DiffWindows& diffWindows, ModalGate& modals, EditorBuffers& buffers,
					 BranchListState& branchList, Core::EventBus& events)
		: Core::Panel("git", "Git", "git", ICON_LC_GIT_BRANCH, StartsAs::Hidden), m_Model(model),
		  m_Scheme(scheme), m_EditorSettings(editorSettings), m_DiffWindows(diffWindows),
		  m_Buffers(buffers), m_Events(events), m_BranchList(branchList), m_DetachQuestion(modals, "###GitDetach"),
		  m_CheckoutQuestion(modals, "###GitCheckout"),
		  m_NewBranchQuestion(modals, "###GitNewBranch"), m_RenameBranchQuestion(modals, "###GitRenameBranch"),
		  m_DeleteBranchQuestion(modals, "###GitDeleteBranch"), m_DropStashQuestion(modals, "###GitDropStash")
	{
		m_Diff.OnSetOptions = [this](const DiffViewOptions& options) { m_EditorSettings.SetDiffViewOptions(options); };

		m_BranchList.OnBranchClicked = [this](const Core::Branch& branch) { SelectBranch(branch); };
		m_BranchList.DrawBranchMenu = [this](const Core::Branch& branch) { RenderRefMenu(branch); };
		m_BranchList.BadgeOf = [this](const Core::Branch& branch) {
			return ReviewBadgeOf ? ReviewBadgeOf(branch) : std::string{};
		};
	}

	void GitView::OnUpdate(Core::Timestep)
	{
		const bool panelOnScreen = HasBeenDrawnOnLatestFrame();

		// WARNING: Begin hands the modal gate back, and it only runs from OnImGuiRender, which is
		// skipped for a panel that is hidden or behind another tab. An undrawn panel withdraws its
		// questions here or it holds the gate for good.
		if (!panelOnScreen)
		{
			HoldAskedQuestionsForPanel();
			WithdrawQuestions();
		}

		FollowSelectedRef();

		if (std::optional<CheckoutPlanAnswer> answer = m_Model.TakeCheckoutPlan();
			answer.has_value() && m_PendingCheckout == answer->Target)
		{
			m_PendingCheckout.reset();
			m_HeldCheckoutPlan = std::move(answer);
		}

		if (m_HeldCheckoutPlan.has_value() &&
			ApplyCheckoutPlan(*m_HeldCheckoutPlan, panelOnScreen) == PlanApplied::Answered)
		{
			m_HeldCheckoutPlan.reset();
		}

		if (std::optional<DeleteCheck> check = m_Model.TakeDeleteCheck())
		{
			ApplyDeleteCheck(*check);
		}

		if (panelOnScreen)
		{
			AskDeleteHeldForPanel();
		}

		if (std::vector<std::filesystem::path> changed = m_Model.TakeCheckedOut(); !changed.empty())
		{
			const std::filesystem::path& root = m_Model.GetWorkingDirectory();
			const bool hasNoWorkingDirectory = root.empty();

			if (hasNoWorkingDirectory)
			{
				return;
			}

			std::vector<std::filesystem::path> changedAbsolute;
			changedAbsolute.reserve(changed.size());

			for (const std::filesystem::path& path : changed)
			{
				changedAbsolute.push_back(root / path);
			}

			m_Buffers.ExpectExternalWrite(SilentReloads(changedAbsolute, m_Buffers.ModifiedUnder(root)));
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

		if (BranchWithFullName(*branches, *m_SelectedRef) != nullptr)
		{
			return;
		}

		const bool isFollowingRename = m_PendingRename.has_value() && m_PendingRename->From == *m_SelectedRef &&
									   BranchWithFullName(*branches, m_PendingRename->To) != nullptr;

		// A rename keeps the history it names, so the reach the reader asked for stays with it; a
		// branch that is gone falls back to HEAD and starts over.
		const HistoryReach reach = isFollowingRename ? m_Reach : HistoryReach{};

		if (isFollowingRename)
		{
			m_SelectedRef = m_PendingRename->To;
		}
		else
		{
			m_SelectedRef.reset();
		}

		m_PendingRename.reset();
		RequestLog(reach);
	}

	void GitView::HoldAskedQuestionsForPanel()
	{
		if (m_CheckoutQuestion.IsAsked() && m_AskedCheckoutPlan.has_value())
		{
			m_HeldCheckoutPlan = std::move(m_AskedCheckoutPlan);
			m_AskedCheckoutPlan.reset();
		}

		if (m_DeleteBranchQuestion.IsAsked() && m_DeleteUnanswered.has_value())
		{
			m_DeleteHeldForPanel = true;
		}
	}

	void GitView::AskDeleteHeldForPanel()
	{
		if (!m_DeleteHeldForPanel || !m_DeleteUnanswered.has_value())
		{
			return;
		}

		m_DeleteHeldForPanel = false;
		m_DeleteBranchQuestion.AskOnce(DeleteBranchPayload{.Branch = *m_DeleteUnanswered});
	}

	GitPanelCheckouts GitView::Checkouts() const
	{
		GitPanelCheckouts checkouts{.Planning = m_PendingCheckout, .Writing = m_Model.GetCheckoutUnderWay()};

		if (m_AskedCheckoutPlan.has_value())
		{
			checkouts.Asking = m_AskedCheckoutPlan->Target;
		}

		if (m_HeldCheckoutPlan.has_value())
		{
			checkouts.HeldForPanel = m_HeldCheckoutPlan->Target;
		}

		if (m_DeleteHeldForPanel)
		{
			checkouts.DeleteHeldForPanel = m_DeleteUnanswered;
		}

		return checkouts;
	}

	GitPanelWait GitView::CurrentWait() const
	{
		return DescribePanelWait(Checkouts());
	}

	void GitView::BeginDelete(const std::string& branch)
	{
		m_PendingDelete = branch;
		m_DeleteUnanswered = branch;
		m_DeleteUnmerged.reset();
		m_DeleteHeldForPanel = false;

		m_Model.RequestDeleteCheck(branch);
		m_DeleteBranchQuestion.AskOnce(DeleteBranchPayload{.Branch = branch});
	}

	void GitView::ApplyDeleteCheck(const DeleteCheck& check)
	{
		const bool isAwaitedCheck = m_PendingDelete == check.Branch;

		if (!isAwaitedCheck)
		{
			return;
		}

		m_PendingDelete.reset();

		if (check.Failure.has_value())
		{
			ShowFailure(m_Events, "Delete branch failed", check.Branch + ": " + check.Failure->Message,
						m_Model.GetWorkingDirectory());
			ForgetDeleteQuestion(check.Branch);
			return;
		}

		if (m_DeleteUnanswered == check.Branch)
		{
			m_DeleteUnmerged = check.Unmerged;
		}
	}

	void GitView::ForgetDeleteQuestion(const std::string& branch)
	{
		m_DeleteBranchQuestion.WithdrawIf([&branch](const DeleteBranchPayload& payload) { return payload.IsAbout(branch); });

		m_DeleteUnanswered.reset();
		m_DeleteUnmerged.reset();
		m_DeleteHeldForPanel = false;
	}

	std::optional<std::size_t> GitView::UnmergedCountFor(const std::string& branch) const
	{
		if (m_DeleteUnanswered != branch)
		{
			return std::nullopt;
		}

		return m_DeleteUnmerged;
	}

	void GitView::WithdrawQuestions()
	{
		if (m_DetachQuestion.IsAsked())
		{
			m_DetachQuestion.Withdraw();
		}

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

		m_CheckoutDialogOpen = false;
		m_IncludeUntracked = false;
		m_StashSaveFailed = false;
		m_BranchNameFor = nullptr;

		m_AskedCheckoutPlan.reset();

		if (!m_DeleteHeldForPanel)
		{
			m_DeleteUnanswered.reset();
			m_DeleteUnmerged.reset();
		}
	}

	void GitView::BeginCheckout(const Core::CheckoutTarget& target)
	{
		m_PendingCheckout = target;
		m_Model.RequestCheckoutPlan(target);
	}

	GitView::PlanApplied GitView::ApplyCheckoutPlan(const CheckoutPlanAnswer& answer, bool panelOnScreen)
	{
		const std::string label = CheckoutTargetLabel(answer.Target);

		if (answer.Failure.has_value() || !answer.Plan.has_value())
		{
			const std::string failureMessage =
				answer.Failure.has_value() ? answer.Failure->Message : std::string("no plan");
			ShowFailure(m_Events, "Checkout failed", label + ": " + failureMessage, m_Model.GetWorkingDirectory());
			return PlanApplied::Answered;
		}

		const std::filesystem::path& root = m_Model.GetWorkingDirectory();
		std::vector<std::filesystem::path> unsaved =
			root.empty() ? std::vector<std::filesystem::path>{} : m_Buffers.ModifiedUnder(root);

		std::vector<std::filesystem::path> unsavedRelative;
		unsavedRelative.reserve(unsaved.size());

		for (const std::filesystem::path& path : unsaved)
		{
			unsavedRelative.push_back(RelativeOrClimbing(path, root));
		}

		if (!NeedsCheckoutDialog(*answer.Plan, unsavedRelative))
		{
			m_Model.RequestCheckout(CheckoutRequest{.Target = answer.Target});
			return PlanApplied::Answered;
		}

		if (!panelOnScreen)
		{
			return PlanApplied::HeldForPanel;
		}

		const Core::Ref<const Core::WorkingTreeStatus> gitStatusIfKnown = m_Model.GetStatus();

		CheckoutQuestion question = BuildCheckoutQuestion(
			label, *answer.Plan, gitStatusIfKnown ? *gitStatusIfKnown : Core::WorkingTreeStatus{}, unsavedRelative);

		const bool untrackedForced = question.UntrackedForced;

		m_CheckoutQuestion.AskOnce(CheckoutQuestionPayload{.Target = answer.Target,
															.Question = std::move(question),
															.UnsavedAbsolute = std::move(unsaved),
															.IncludeUntracked = untrackedForced});
		m_AskedCheckoutPlan = answer;

		return PlanApplied::Answered;
	}

	void GitView::BeginNewBranch(std::string from, const Core::Oid& at, std::string_view suggested)
	{
		BranchNamePayload payload{.Mode = BranchNameMode::New, .From = std::move(from), .At = at};

		const std::size_t copiedLength = std::min(suggested.size(), payload.Buffer.size() - 1);
		std::copy_n(suggested.begin(), copiedLength, payload.Buffer.begin());

		m_NewBranchQuestion.AskOnce(std::move(payload));
	}

	void GitView::BeginRename(const std::string& branch)
	{
		BranchNamePayload payload{.Mode = BranchNameMode::Rename, .From = branch};

		const std::size_t copiedLength = std::min(branch.size(), payload.Buffer.size() - 1);
		std::copy_n(branch.begin(), copiedLength, payload.Buffer.begin());

		m_RenameBranchQuestion.AskOnce(std::move(payload));
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
		{
			const Core::CheckoutTarget target{.Kind = Core::CheckoutKind::Commit, .Commit = action->Commit};

			if (AsksBeforeDetaching(target))
			{
				m_DetachQuestion.AskOnce(DetachPayload{.Commit = action->Commit});
			}
			else
			{
				BeginCheckout(target);
			}

			break;
		}
		case CommitAction::Kind::Copy:
			CopyToClipboard(m_Events, action->What, action->Text);
			break;
		case CommitAction::Kind::NewBranch:
			BeginNewBranch(action->Commit.ShortHex(), action->Commit);
			break;
		}
	}

	void GitView::Subscribe()
	{
		m_Diff.OnCopied = [this](std::string_view text) {
			ReportCopied(m_Events, "selection", text);
		};

		m_Events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) { ForgetRepository(); });
		m_Events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) { ForgetRepository(); });
	}

	void GitView::ForgetRepository()
	{
		m_LogRequested = false;
		m_Reach = HistoryReach{};
		m_AskedLogGeneration = 0;
		m_Commits.Reset();

		m_FilterBuffer = {};
		m_FilterPathBuffer = {};
		m_BranchList.Reset();
		m_Filter.Reset();

		m_SelectedRef.reset();
		m_SelectedStash.reset();
		m_LastSeenBranches.reset();
		m_PendingRename.reset();
		m_BaseSeen.reset();

		m_LastSeenPage.reset();
		m_GraphOf.reset();
		m_Graph = Core::CommitGraph{};

		m_HeldCheckoutPlan.reset();
		m_DeleteHeldForPanel = false;

		// WARNING: Withdraw is only valid outside a popup, and this runs from an event handler.
		WithdrawQuestions();
		m_PendingCheckout.reset();
		m_PendingDelete.reset();
	}

	void GitView::OnImGuiRender()
	{
		UpdateForNewPage();

		// WARNING: With no folder nothing is on its way, so a loader here would turn forever and keep
		// every frame drawing.
		if (!m_Model.HasOpenFolder())
		{
			WithdrawQuestions();
			Core::EmptyState("No folder is open.");
			return;
		}

		if (IsMissingRepository(m_Model.GetStatus() != nullptr, m_Model.GetStatusError()))
		{
			// WARNING: The dialogs below are not drawn on this path, and Begin is what hands the gate
			// back, so a question left standing would hold it for good.
			WithdrawQuestions();
			DrawInitRepositoryPrompt(m_Model);
			return;
		}

		const GitPalette palette = ReadGitPalette(m_Scheme.Get());

		if (ImGui::BeginTable("git", kGitTableColumnCount, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("refs", ImGuiTableColumnFlags_WidthFixed,
									ImGui::GetFontSize() * kRefsColumnWidthInFontSizes);
			ImGui::TableSetupColumn("history", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(kRefsColumn);
			RenderRefs();

			ImGui::TableSetColumnIndex(kHistoryColumn);

			RenderFilterBar();

			const std::function<void()> drawList = [this, &palette] { RenderCommits(palette); };
			const std::function<void()> drawDiff = [this, &palette] { RenderPreview(palette); };

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
			ImGui::TextUnformatted(error->Message.c_str());
		}

		RenderDetachDialog();
		RenderCheckoutDialog();
		RenderBranchNameDialog(m_NewBranchQuestion);
		RenderBranchNameDialog(m_RenameBranchQuestion);
		RenderDeleteBranchDialog();
		RenderDropStashDialog();
	}

	BranchListFrame GitView::BranchListFrameNow() const
	{
		BranchListFrame frame;
		frame.Branches = m_Model.GetBranches();
		frame.BranchesFailed = m_Model.GetBranchesError().has_value();
		frame.Authors = m_Model.GetBranchAuthors();

		if (const std::optional<Core::Error>& authorsError = m_Model.GetBranchAuthorsError(); authorsError.has_value())
		{
			frame.AuthorsFailure = authorsError->Message;
		}

		frame.AuthorsInFlight = m_Model.IsBranchAuthorsInFlight();
		frame.SelectedRef = m_SelectedStash.has_value() ? std::nullopt : m_SelectedRef;
		frame.CheckingOut = CheckoutMarkedOnRows(Checkouts());
		frame.Now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
		frame.LocalOffset = LocalOffset();

		return frame;
	}

	void GitView::RenderRefs()
	{
		const BranchListFrame frame = BranchListFrameNow();

		m_BranchList.DrawHeader(frame);

		const ImVec2 fillTheRest{0.0f, 0.0f};

		if (ImGui::BeginChild("##refsList", fillTheRest))
		{
			const ListBodyScope listBody;
			ImGui::Indent(ListRowEndInset());
			m_BranchList.DrawRows(frame);

			// WARNING: Drawn before the branch list is read, the stashes sit at the top, then under
			// the loader, then jump to the end once the branches land.
			const bool branchListSettled = frame.Branches || frame.BranchesFailed;

			if (branchListSettled)
			{
				RenderStashes();
			}

			ImGui::Unindent(ListRowEndInset());
		}

		ImGui::EndChild();
	}

	void GitView::SelectBranch(const Core::Branch& branch)
	{
		m_SelectedRef = branch.FullName;

		DeselectStash();

		RequestLog(HistoryReach{});
	}

	void GitView::RenderRefMenu(const Core::Branch& branch)
	{
		const ListPopupScope popup;

		if (!Core::BeginRightClickPopup("ref-menu"))
		{
			return;
		}

		const bool notSelected = false;
		const char* const noShortcut = nullptr;

		ImGui::BeginDisabled(m_Model.IsWriteUnsettled());

		if (branch.Kind == Core::RefKind::Local)
		{
			if (ImGui::MenuItem("Checkout", noShortcut, notSelected, !branch.IsHead))
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

			if (ImGui::MenuItem("Delete", noShortcut, notSelected, !branch.IsHead))
			{
				BeginDelete(branch.Name);
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
				const std::size_t slash = branch.Name.find('/');
				const bool hasRemotePrefix = slash != std::string::npos;
				const std::string_view localName =
					hasRemotePrefix ? std::string_view(branch.Name).substr(slash + 1) : std::string_view{};

				BeginNewBranch(branch.Name, branch.Tip, localName);
			}
		}

		ImGui::EndDisabled();
		RenderReviewItem(branch);

		ImGui::Separator();

		if (ImGui::MenuItem("Copy Branch Name"))
		{
			CopyToClipboard(m_Events, "branch name", branch.Name);
		}

		if (ImGui::MenuItem("Copy Tip Hash"))
		{
			CopyToClipboard(m_Events, "tip hash", branch.Tip.ToHex());
		}

		ImGui::EndPopup();
	}

	void GitView::RenderReviewItem(const Core::Branch& branch)
	{
		if (!ReviewMenuOf)
		{
			return;
		}

		const ReviewMenu menu = ReviewMenuOf(branch);

		if (menu.Items.empty())
		{
			return;
		}

		ImGui::Separator();

		const bool inSubmenu = !menu.Submenu.empty();

		if (inSubmenu && !ImGui::BeginMenu(menu.Submenu.c_str()))
		{
			return;
		}

		for (std::size_t itemIndex = 0; itemIndex < menu.Items.size(); ++itemIndex)
		{
			ImGui::PushID(static_cast<int>(itemIndex));

			if (ImGui::MenuItem(menu.Items[itemIndex].Label.c_str()) && OnReviewMenuItem)
			{
				OnReviewMenuItem(menu.Items[itemIndex]);
			}

			ImGui::PopID();
		}

		if (inSubmenu)
		{
			ImGui::EndMenu();
		}
	}

	std::chrono::minutes GitView::LocalOffset()
	{
		// WARNING: The offset is resolved on every call and never cached, since it changes at a
		// daylight-saving boundary.
		const std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());

		return Core::OffsetAt(Core::LocalTimeZone(), now);
	}

	void GitView::RequestLog(const HistoryReach& reach, LogAsk ask)
	{
		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

		m_Reach = reach;

		m_Model.RequestLog(BuildLogRequest(m_SelectedRef, m_Filter.Get(), m_Reach, m_SelectedStash),
						   branches ? OwnCommitsToAsk(*branches) : std::nullopt);

		// WARNING: Accept tells the settling filter that this query already carries what is typed,
		// or its wait would run out afterwards and send the same query a second time.
		m_LogRequested = true;
		m_Filter.Accept();

		if (ask == LogAsk::ByReader)
		{
			m_AskedLogGeneration = m_Model.GetLogGeneration();
		}
	}

	std::optional<GitView::BaseSeen> GitView::BaseOf(const std::optional<ForkBase>& fork)
	{
		if (!fork.has_value())
		{
			return std::nullopt;
		}

		return BaseSeen{.Branch = *fork->OwnCommits.Ref, .BaseTip = *fork->OwnCommits.Exclude};
	}

	std::optional<ForkSeparator> GitView::ForkSeparatorFor(const Core::LogPage& page) const
	{
		const Core::Ref<const Core::LogPage> own = m_Model.GetOwnCommits();

		if (!own)
		{
			return std::nullopt;
		}

		// WARNING: The own commits land with the log they were asked with, so they belong to this page
		// unless one of them was refreshed alone. A page that starts elsewhere is not theirs.
		const bool startsAtOwnTip = own->Commits.empty() || (!page.Commits.empty() &&
															  page.Commits.front().Id == own->Commits.front().Id);

		if (!startsAtOwnTip)
		{
			return std::nullopt;
		}

		return FindForkSeparator(page, *own, m_Model.GetOwnCommitsLabel());
	}

	std::optional<GitModel::OwnCommitsRequest> GitView::OwnCommitsToAsk(const std::vector<Core::Branch>& branches)
	{
		const std::optional<ForkBase> fork = FindForkBase(m_SelectedRef, branches);

		// WARNING: What was seen is kept even when a filter keeps the own commits from being asked, or
		// the log would be asked again every frame that the filter and the base disagree.
		m_BaseSeen = BaseOf(fork);

		const HistoryFilter& filter = m_Filter.Get();
		const bool showsWholeHistory = filter.Text.empty() && Core::Trimmed(filter.Path).empty() &&
									   !m_SelectedStash.has_value();

		if (!fork.has_value() || !showsWholeHistory)
		{
			return std::nullopt;
		}

		return GitModel::OwnCommitsRequest{.Query = fork->OwnCommits, .Label = fork->BaseName};
	}

	void GitView::AskAgainWhenBaseMoved(const std::vector<Core::Branch>& branches)
	{
		if (BaseOf(FindForkBase(m_SelectedRef, branches)) == m_BaseSeen)
		{
			return;
		}

		RequestLog(m_Reach, LogAsk::Again);
	}

	void GitView::UpdateForNewPage()
	{
		const Core::Ref<const Core::LogPage> page = m_Model.GetLog();

		if (page != m_LastSeenPage)
		{
			m_LastSeenPage = page;

			if (page)
			{
				m_Commits.KeepSelectionWithin(*page);
			}
			else
			{
				m_Commits.ClearSelection();
			}
		}
	}

	void GitView::RenderFilterBar()
	{
		const float pathWidth = ImGui::GetFontSize() * kPathBoxWidthInFontSizes;
		const float wordsWidth = ImGui::GetContentRegionAvail().x - pathWidth - ImGui::GetStyle().ItemSpacing.x;

		DrawFilterField("##commitFilter", m_FilterBuffer.data(), m_FilterBuffer.size(),
						{.Hint = "Message, author or hash", .Width = wordsWidth});

		ImGui::SameLine();
		DrawFilterField("##commitPath", m_FilterPathBuffer.data(), m_FilterPathBuffer.size(),
						{.Hint = "Main.cpp or *.cpp", .Glyph = ICON_LC_FILE, .Width = pathWidth});

		m_Filter.Set(HistoryFilter{std::string(m_FilterBuffer.data()),
								   std::string(m_FilterPathBuffer.data())});

		const bool filterSettled = m_Filter.Advance(Core::Timestep{ImGui::GetIO().DeltaTime});

		if (filterSettled)
		{
			RequestLog(HistoryReach{});
		}
	}

	void GitView::RenderCommits(const GitPalette& palette)
	{
		const Core::Ref<const Core::LogPage> page = m_Model.GetLog();

		if (!page && !m_LogRequested)
		{
			RequestLog(HistoryReach{});
		}

		if (!page)
		{
			const std::optional<std::string> placeholder =
				HistoryPlaceholder(m_Model.GetStatus() != nullptr, m_Model.GetStatusError(), m_Model.GetLogError());

			if (placeholder.has_value())
			{
				Core::EmptyState(*placeholder);
			}
			else
			{
				Core::LoadingState("Loading history");
			}
		}
		else
		{
			if (page != m_GraphOf)
			{
				m_GraphOf = page;
				m_Graph = Core::LayoutCommits(page->Commits);
			}

			const auto diffOf = [this](const Core::Oid& commit) {
				return m_Model.GetDiffFor(commit);
			};

			m_Commits.SetActionsEnabled(!m_Model.IsWriteUnsettled());
			const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

			if (branches != m_RefsOf)
			{
				m_RefsOf = branches;
				m_CommitRefs = BuildCommitRefs(branches ? *branches : std::vector<Core::Branch>{});
			}

			const auto diffFailed = [this](const Core::Oid& commit) { return m_Model.IsDiffFailed(commit); };

			if (branches)
			{
				AskAgainWhenBaseMoved(*branches);
			}

			const std::optional<ForkSeparator> separator = ForkSeparatorFor(*page);

			const std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInputFor(*page));

			m_Commits.SetRoomBelow(status.has_value() ? HistoryStatusLineHeight() : 0.0f);
			m_Commits.SetEmptyText(IsShownLogNarrowed() ? "No matching commits." : "No commits yet.");

			m_Commits.Render(*page, m_Graph, palette, LocalOffset(), diffOf, diffFailed, m_CommitRefs,
							 m_Filter.Get().Text, m_Filter.Get().Path, separator);

			// WARNING: Drawn straight after the list, before anything else moves the cursor, so it sits
			// in the room the list left for it.
			std::optional<HistoryClick> click;

			if (status.has_value())
			{
				click = DrawHistoryStatusLine(*status);
			}

			ApplyCommitAction();

			std::unordered_set<Core::Oid> wanted;

			const std::optional<Core::Oid> selectedCommit = m_Commits.GetSelectedCommit();

			for (const Core::CommitSummary& commit : page->Commits)
			{
				const bool isWanted = m_Commits.IsExpanded(commit.Id) || selectedCommit == commit.Id;

				if (!isWanted)
				{
					continue;
				}

				wanted.insert(commit.Id);

				if (!m_Model.GetDiffFor(commit.Id))
				{
					m_Model.RequestDiffFor(commit.Id);
				}
			}

			// WARNING: A successful diff stays cached, so collapsing and re-expanding a row does not
			// re-query git. Only a failed entry is forgotten here, which is what gives a failure a way
			// back: the same gesture that asks for a diff asks again.
			for (const Core::Oid& commit : m_PreviouslyWanted)
			{
				if (!wanted.contains(commit))
				{
					m_Model.ForgetFailedDiffFor(commit);
				}
			}

			m_PreviouslyWanted = std::move(wanted);

			if (click.has_value())
			{
				AnswerHistoryClick(*click, *page);
			}
		}
	}

	HistoryStatusInput GitView::HistoryStatusInputFor(const Core::LogPage& page) const
	{
		const std::optional<Core::LogRequest>& shown = m_Model.GetShownLogRequest();
		const std::optional<Core::LogRequest>& asked = m_Model.GetAskedLogRequest();

		return HistoryStatusInput{.Shown = page.Commits.size(),
								  .Walked = page.Walked,
								  .Complete = page.Complete,
								  .Filtered = IsShownLogNarrowed(),
								  .Stash = shown.has_value() && shown->Stash.has_value(),
								  .Total = m_Model.GetCommitCountFor(shown.has_value() ? shown->Ref : std::nullopt),
								  .Counting = m_Model.IsCommitCountInFlight(),
								  .Asking = !m_Model.IsLogAnswered(m_AskedLogGeneration),
								  .AskedFiltered = asked.has_value() && NarrowsHistory(*asked)};
	}

	bool GitView::IsShownLogNarrowed() const
	{
		const std::optional<Core::LogRequest>& shown = m_Model.GetShownLogRequest();

		return shown.has_value() && NarrowsHistory(*shown);
	}

	void GitView::AnswerHistoryClick(HistoryClick click, const Core::LogPage& page)
	{
		const std::optional<Core::LogRequest>& shown = m_Model.GetShownLogRequest();

		if (!shown.has_value())
		{
			return;
		}

		RequestLog(ReachAfterClick(click == HistoryClick::All, NarrowsHistory(*shown),
								   HistoryReach{.MaxCount = shown->MaxCount, .MaxWalk = shown->MaxWalk},
								   page.Commits.size(), page.Walked));
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

		// WARNING: UpdateForNewPage runs at the top of the frame but the model is read again here, so
		// a selection can outlive the page that carried it. The empty pane is the fallback.
		RenderDiff(palette);
	}

	void GitView::RenderDetails(const Core::CommitSummary& commit, const GitPalette& palette)
	{
		std::optional<DiffStats> stats;

		if (const Core::Ref<const Core::Diff> diff = m_Model.GetDiffFor(commit.Id))
		{
			stats = CountChanges(*diff);
		}

		if (Graphite::RenderCommitDetails(commit, stats, LocalOffset(), palette))
		{
			m_Commits.ClearCommitSelection();
		}
	}

	void GitView::RenderDiff(const GitPalette& palette)
	{
		const std::optional<FileSelection> selected = m_Commits.GetSelectedFile();

		if (!selected.has_value())
		{
			m_Diff.OnOpenAsTab = nullptr;
			m_Diff.OnClose = nullptr;
			m_Diff.OnOpenFile = nullptr;
			Core::EmptyState("Select a commit or a file to see what changed.");
			return;
		}

		const OpenDiff selection{
			.Commit = selected->Commit, .Target = Core::DiffTarget::CommitToParent, .Path = selected->Path};

		// WARNING: m_Diff is the one bottom pane shared by every selection, so its callbacks capture
		// the current one and are reassigned every frame, before the early returns below, or one
		// would keep a previous frame's selection while this diff loads.
		m_Diff.OnOpenAsTab = [this, selection] { m_DiffWindows.Open(selection); };

		m_Diff.OnClose = [this] { m_Commits.ClearSelection(); };

		m_Diff.OnOpenFile = m_DiffWindows.OpenFileIn(m_Model);

		const Core::Ref<const Core::Diff> diff = m_Model.GetDiffFor(selected->Commit);

		if (!diff && m_Model.IsDiffFailed(selected->Commit))
		{
			Core::EmptyState("The diff could not be read.");
			return;
		}

		if (!diff)
		{
			Core::LoadingState("Loading the diff");
			return;
		}

		const std::optional<std::size_t> fileIndex = IndexOfFile(*diff, selected->Path);

		if (!fileIndex.has_value())
		{
			Core::EmptyState("That file is not in this diff.");
			return;
		}

		const Core::FileDiff& file = diff->Files[*fileIndex];

		m_Texts.Update(m_Model, selected->Commit, file);

		const SyntaxPalette syntax = ReadSyntaxPalette(m_Scheme.Get());
		m_Diff.Render(file, palette, syntax, ReadEditorPalette(m_Scheme.Get()).Selection, m_Texts.Texts,
					  m_Texts.Tokens, m_EditorSettings.GetDiffViewOptions());
	}
}
