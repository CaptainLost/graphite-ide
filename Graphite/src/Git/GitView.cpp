#include "Git/GitView.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Base.h"
#include "Core/Base/Strings.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/Platform/Shell.h"

#include "Theme/Palettes.h"
#include "Theme/GitPalette.h"
#include "UI/ClipboardCopy.h"
#include "UI/Lists/FilterField.h"
#include "UI/Lists/ListRow.h"
#include "Git/Branches/BranchLookup.h"
#include "Git/Compare/HeaderShares.h"
#include "Git/Compare/BranchMenu.h"
#include "Git/GitRequests.h"
#include "Git/History/CommitRows.h"
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

		constexpr float kPathBoxWidthInFontSizes = 14.0f;
		constexpr float kFilterFieldPixels = 200.0f; // the mockup's width of the history's filter field
		constexpr int kGitTableColumnCount = 2;
		constexpr int kSidebarColumn = 0;
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

	GitView::GitView(GitModel& model, LinesModel& lines, WorktreesModel& worktrees, ChangesView& changes, const ActiveScheme& scheme,
					 EditorSettings& editorSettings, DiffWindows& diffWindows, ModalGate& modals, EditorBuffers& buffers,
					 Core::EventBus& events)
		: Core::Panel("git", "Git", "git", ICON_LC_GIT_BRANCH, StartsAs::Hidden), m_Model(model), m_Lines(lines), m_Worktrees(worktrees),
		  m_Changes(changes), m_Scheme(scheme), m_EditorSettings(editorSettings), m_DiffWindows(diffWindows),
		  m_Buffers(buffers), m_Events(events), m_DetachQuestion(modals, "###GitDetach"),
		  m_CheckoutQuestion(modals, "###GitCheckout"),
		  m_NewBranchQuestion(modals, "###GitNewBranch"), m_RenameBranchQuestion(modals, "###GitRenameBranch"),
		  m_DeleteBranchQuestion(modals, "###GitDeleteBranch"), m_DropStashQuestion(modals, "###GitDropStash")
	{
		m_Diff.OnSetOptions = [this](const DiffViewOptions& options) { m_EditorSettings.SetDiffViewOptions(options); };

		ConnectViews();
	}

	void GitView::OnUpdate(Core::Timestep)
	{
		const bool panelOnScreen = HasBeenDrawnOnLatestFrame();
		m_Worktrees.SetOnScreen(panelOnScreen);

		// WARNING: Begin hands the modal gate back, and it only runs from OnImGuiRender, which is
		// skipped for a panel that is hidden or behind another tab. An undrawn panel withdraws its
		// questions here or it holds the gate for good.
		if (!panelOnScreen)
		{
			HoldAskedQuestionsForPanel();
			WithdrawQuestions();
		}

		// WARNING: The same holds for the Changes view, which is drawn only while it is the view on screen.
		if (!m_ChangesDrawn)
		{
			m_Changes.WithdrawQuestions();
		}

		m_ChangesDrawn = false;

		ApplyPendingRestore();
		FollowSelectedRef();
		FollowSelectedStash();

		if (const std::optional<Core::CheckoutTarget> heldTarget = m_HeldCheckout.TakeOnceOnScreen(panelOnScreen))
		{
			BeginCheckout(*heldTarget);
		}

		if (const std::optional<CheckoutPlanAnswer> answer = m_Model.TakeCheckoutPlan();
			answer.has_value() && m_PendingCheckout == answer->Target)
		{
			m_PendingCheckout.reset();
			ApplyCheckoutPlan(*answer, panelOnScreen);
		}

		if (std::optional<DeleteCheck> check = m_Model.TakeDeleteCheck())
		{
			ApplyDeleteCheck(*check);
		}

		if (panelOnScreen)
		{
			AskDeleteHeldForPanel();
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

		if (!branches)
		{
			return;
		}

		const bool renameLanded = m_PendingRename.has_value() &&
								  BranchWithFullName(*branches, m_PendingRename->From) == nullptr &&
								  BranchWithFullName(*branches, m_PendingRename->To) != nullptr;

		FollowHistoryRef(*branches, renameLanded);

		if (renameLanded)
		{
			m_PendingRename.reset();
		}
	}

	void GitView::FollowHistoryRef(const std::vector<Core::Branch>& branches, bool renameLanded)
	{
		if (!m_SelectedRef.has_value() || BranchWithFullName(branches, *m_SelectedRef) != nullptr)
		{
			return;
		}

		const bool isFollowingRename = renameLanded && m_PendingRename->From == *m_SelectedRef;

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
		if (m_CheckoutQuestion.IsAsked() && m_AskedCheckout.has_value())
		{
			m_HeldCheckout.Hold(*m_AskedCheckout);
			m_AskedCheckout.reset();
		}

		if (m_DeleteBranchQuestion.IsAsked())
		{
			m_Deletion.HoldForPanel();
		}
	}

	void GitView::AskDeleteHeldForPanel()
	{
		constexpr bool kPanelOnScreen = true;

		if (const std::optional<std::string> branch = m_Deletion.TakeHeldOnceOnScreen(kPanelOnScreen))
		{
			m_DeleteBranchQuestion.AskOnce(DeleteBranchPayload{.Branch = *branch});
		}
	}

	GitPanelCheckouts GitView::Checkouts() const
	{
		GitPanelCheckouts checkouts{.Planning = m_PendingCheckout,
									.Asking = m_AskedCheckout,
									.HeldForPanel = m_HeldCheckout.HeldTarget(),
									.Writing = m_Model.GetCheckoutUnderWay()};

		if (m_Deletion.IsHeldForPanel())
		{
			checkouts.DeleteHeldForPanel = m_Deletion.UnansweredBranch();
		}

		return checkouts;
	}

	GitPanelWait GitView::CurrentWait() const
	{
		return DescribePanelWait(Checkouts());
	}

	void GitView::BeginDelete(const std::string& branch)
	{
		m_Deletion.Begin(branch);

		m_Model.RequestDeleteCheck(branch);
		m_DeleteBranchQuestion.AskOnce(DeleteBranchPayload{.Branch = branch});
	}

	void GitView::ApplyDeleteCheck(const DeleteCheck& check)
	{
		if (m_Deletion.Land(check) != DeleteCheckLanding::Failed)
		{
			return;
		}

		ShowFailure(m_Events, "Delete branch failed", check.Branch + ": " + check.Failure->Message,
					m_Model.GetWorkingDirectory());

		const std::string& branch = check.Branch;
		m_DeleteBranchQuestion.WithdrawIf([&branch](const DeleteBranchPayload& payload) { return payload.IsAbout(branch); });
	}

	void GitView::FinishDeleteQuestion()
	{
		m_DeleteBranchQuestion.Answer();
		m_Deletion.Finish();
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

		m_AskedCheckout.reset();
		m_Deletion.ForgetUnlessHeld();
	}

	void GitView::BeginCheckout(const Core::CheckoutTarget& target)
	{
		m_PendingCheckout = target;
		m_Model.RequestCheckoutPlan(target);
	}

	void GitView::ApplyCheckoutPlan(const CheckoutPlanAnswer& answer, bool panelOnScreen)
	{
		const std::filesystem::path& root = m_Model.GetWorkingDirectory();
		std::vector<std::filesystem::path> unsaved =
			root.empty() ? std::vector<std::filesystem::path>{} : m_Buffers.ModifiedUnder(root);

		std::vector<std::filesystem::path> unsavedRelative;
		unsavedRelative.reserve(unsaved.size());

		for (const std::filesystem::path& path : unsaved)
		{
			unsavedRelative.push_back(RelativeOrClimbing(path, root));
		}

		const bool planFailed = answer.Failure.has_value() || !answer.Plan.has_value();
		const bool needsDialog = !planFailed && NeedsCheckoutDialog(*answer.Plan, unsavedRelative);

		switch (DecideOnCheckoutPlan({.Failed = planFailed, .NeedsDialog = needsDialog, .PanelOnScreen = panelOnScreen}))
		{
		case CheckoutPlanVerdict::ReportFailure:
		{
			const std::string failureMessage =
				answer.Failure.has_value() ? answer.Failure->Message : std::string("no plan");
			ShowFailure(m_Events, "Checkout failed", CheckoutTargetLabel(answer.Target) + ": " + failureMessage,
						m_Model.GetWorkingDirectory());
			break;
		}

		case CheckoutPlanVerdict::CheckOut:
			m_Model.RequestCheckout(CheckoutRequest{.Target = answer.Target});
			break;

		case CheckoutPlanVerdict::Ask:
			AskCheckoutQuestion(answer, std::move(unsaved), unsavedRelative);
			break;

		case CheckoutPlanVerdict::HoldForPanel:
			m_HeldCheckout.Hold(answer.Target);
			break;
		}
	}

	void GitView::AskCheckoutQuestion(const CheckoutPlanAnswer& answer, std::vector<std::filesystem::path> unsaved,
									  const std::vector<std::filesystem::path>& unsavedRelative)
	{
		const Core::Ref<const Core::WorkingTreeStatus> gitStatusIfKnown = m_Model.GetStatus();

		CheckoutQuestion question =
			BuildCheckoutQuestion(CheckoutTargetLabel(answer.Target), *answer.Plan,
								  gitStatusIfKnown ? *gitStatusIfKnown : Core::WorkingTreeStatus{}, unsavedRelative);

		const bool untrackedForced = question.UntrackedForced;

		m_CheckoutQuestion.AskOnce(CheckoutQuestionPayload{.Target = answer.Target,
															.Question = std::move(question),
															.UnsavedAbsolute = std::move(unsaved),
															.IncludeUntracked = untrackedForced});
		m_AskedCheckout = answer.Target;
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

		m_Events.Subscribe<ShowFileHistoryRequested>([this](const ShowFileHistoryRequested& request) {
			ShowFileHistory(request.RelativePath);
		});

		m_Events.Subscribe<ShowCommitRequested>([this](const ShowCommitRequested& request) {
			ShowCommit(CommitReveal{.Commit = request.Commit, .Path = request.RelativePath});
		});
	}

	void GitView::ForgetRepository()
	{
		m_LogRequested = false;
		m_Reach = HistoryReach{};
		m_AskedLogGeneration = 0;
		m_Commits.Reset();

		m_FilterBuffer = {};
		m_FilterPathBuffer = {};
		m_Filter.Reset();
		m_FirstParentsOnly = true;

		m_SelectedRef.reset();
		m_SelectedStash.reset();
		m_LastSeenBranches.reset();
		m_PendingRename.reset();
		m_LineAsked.reset();
		m_LogHeld = false;
		m_OpeningRef = false;
		m_NewerShown.clear();
		m_MarkedOf.reset();
		m_Marked.clear();
		m_FoldOf.reset();
		m_Fold.reset();
		m_RowsShownFor.reset();
		m_WindowBarSource.reset();
		m_WindowBarSettled.reset();
		m_MainLineDismissed = false;

		// WARNING: Subscribe runs before GraphiteLayer subscribes its own handlers, so a history the opening folder
		// restores from its handler of the same event is asked for after this has let go of the last one.
		m_PendingRestore.reset();

		// WARNING: The lines view into the read held with the rows, so they go first.
		m_SidebarLines.reset();
		m_SidebarBuiltFrom.reset();
		m_SidebarRows.clear();
		m_MostCommonLine.clear();
		m_HeadRef.reset();
		m_OpenByUser.clear();
		m_ShowingMerged.clear();
		++m_SidebarChoicesVersion;

		m_ChangesPicked = false;
		m_ChangesDiff.Diff = {};

		m_LastSeenPage.reset();
		m_GraphOf.reset();
		m_Graph = Core::CommitGraph{};

		m_HeldCheckout.Forget();
		m_Deletion.Finish();

		m_PendingReveal.reset();
		m_RevealedFile.reset();

		// WARNING: Withdraw is only valid outside a popup, and this runs from an event handler.
		WithdrawQuestions();
		m_PendingCheckout.reset();
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

		RenderWindowBar();

		// WARNING: Not the "gitWindow" of the navigator this replaced, so the sidebar opens at its own width rather than
		// at the width ImGui saved for the navigator's column.
		if (ImGui::BeginTable("gitWindowA", kGitTableColumnCount, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("sidebar", ImGuiTableColumnFlags_WidthFixed, SidebarColumnWidth());
			ImGui::TableSetupColumn("history", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(kSidebarColumn);
			RenderSidebar();

			ImGui::TableSetColumnIndex(kHistoryColumn);
			RenderHistory(palette);

			ImGui::EndTable();
		}

		RenderDetachDialog();
		RenderCheckoutDialog();
		RenderBranchNameDialog(m_NewBranchQuestion);
		RenderBranchNameDialog(m_RenameBranchQuestion);
		RenderDeleteBranchDialog();
		RenderDropStashDialog();
	}

	void GitView::RenderHistory(const GitPalette& palette)
	{
		AskLogAsNeeded();
		RenderHistoryHeader();

		const std::function<void()> drawList = [this, &palette] { RenderCommits(palette); };
		const std::function<void()> drawDiff = [this, &palette] { RenderPreview(palette); };

		const bool hasSelection = m_Commits.GetSelectedFile().has_value() || m_Commits.GetSelectedCommit().has_value() ||
								  m_Commits.IsLocalChangesSelected();

		DrawSplitResults(SplitResultsInput{.HasSelection = hasSelection,
										   .ResultsHeight = m_ListHeight,
										   .DrawResults = drawList,
										   .DrawPreview = drawDiff,
										   .ResultsChildId = "##gitCommits",
										   .PreviewChildId = "##gitDiff",
										   .SplitterId = "##gitSplit"});
	}

	void GitView::SelectRef(std::optional<std::string> fullName)
	{
		m_PendingRestore.reset();
		m_SelectedRef = std::move(fullName);

		DeselectStash();
		StartOpening();

		RequestLog(HistoryReach{});
	}

	void GitView::StartOpening()
	{
		// The checked-out branch opens on its Local changes; any other reference on the row its first page gives.
		m_OpeningRef = !ShowsLocalChanges();
		m_Commits.ClearSelection();

		if (m_OpeningRef)
		{
			m_Commits.ClearRowSelection();
		}
		else
		{
			m_Commits.SelectLocalChanges();
			m_Commits.ScrollToTop();
		}
	}

	void GitView::RenderRefMenu(const Core::Branch& branch)
	{
		const ListPopupScope popup;

		if (!Core::BeginRightClickPopup("ref-menu"))
		{
			return;
		}

		RenderRefMenuItems(branch);
		ImGui::EndPopup();
	}

	void GitView::RenderRefMenuItems(const Core::Branch& branch)
	{
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
		RenderWorktreeItem(branch);
		RenderReviewItem(branch);
		RenderLineItems(branch);

		ImGui::Separator();

		if (ImGui::MenuItem("Copy Branch Name"))
		{
			CopyToClipboard(m_Events, "branch name", branch.Name);
		}

		if (ImGui::MenuItem("Copy Tip Hash"))
		{
			CopyToClipboard(m_Events, "tip hash", branch.Tip.ToHex());
		}
	}

	void GitView::RenderWorktreeItem(const Core::Branch& branch)
	{
		if (branch.Kind != Core::RefKind::Local)
		{
			return;
		}

		const std::optional<std::filesystem::path> folder = m_Worktrees.WorktreeOf(branch.FullName);

		if (!folder.has_value())
		{
			return;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Show worktree folder"))
		{
			const Core::Result<void> opened = Core::Shell::OpenFolder(*folder);

			if (!opened.HasValue())
			{
				m_Events.Publish(FolderToast(Core::ToastKind::Error, "Cannot open folder", opened.GetError().Message,
											 m_Model.GetWorkingDirectory()));
			}
		}
	}

	void GitView::RenderLineItems(const Core::Branch& branch)
	{
		const Core::Ref<const LineRead> read = m_Lines.GetRead();
		const BranchLines* const lines = CurrentLines();

		// A base's local copy answers for the base its row shows.
		const std::string shown = lines != nullptr ? std::string{lines->ShownAs(branch.FullName)} : branch.FullName;
		const BranchMenu menu = MenuFor(read.get(), lines, shown);

		if (!menu.CompareWith && !menu.UseAsBase && !menu.StopUsingAsBase && !menu.UseAsMainLine)
		{
			return;
		}

		ImGui::Separator();

		if (menu.CompareWith && ImGui::MenuItem("Compare with…"))
		{
			SelectRef(branch.FullName);
			m_VsPicker.Open(branch.FullName, ImGui::GetMousePos());
		}

		if (menu.UseAsBase && ImGui::MenuItem("Use as base"))
		{
			m_Lines.Change(LineAnswerChange{.What = LineAnswerChange::Kind::UseAsBase, .Ref = shown});
		}

		if (menu.StopUsingAsBase && ImGui::MenuItem("Stop using as base"))
		{
			m_Lines.Change(LineAnswerChange{.What = LineAnswerChange::Kind::StopUsingAsBase, .Ref = shown});
		}

		if (menu.UseAsMainLine && ImGui::MenuItem("Use as main line"))
		{
			m_Lines.Change(LineAnswerChange{.What = LineAnswerChange::Kind::UseAsMainLine, .Ref = shown});
		}
	}

	void GitView::RenderReviewItem(const Core::Branch& branch)
	{
		if (!ReviewMenuOf)
		{
			return;
		}

		const ReviewMenu menu = ReviewMenuOf(branch);

		const bool hasSubmenu = !menu.Submenu.empty() && !menu.SubmenuItems.empty();

		if (menu.Items.empty() && !hasSubmenu)
		{
			return;
		}

		ImGui::Separator();

		DrawReviewMenuItems(menu.Items);

		if (hasSubmenu && ImGui::BeginMenu(menu.Submenu.c_str()))
		{
			DrawReviewMenuItems(menu.SubmenuItems);
			ImGui::EndMenu();
		}
	}

	void GitView::DrawReviewMenuItems(const std::vector<ReviewMenuItem>& items)
	{
		for (std::size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
		{
			ImGui::PushID(static_cast<int>(itemIndex));

			if (ImGui::MenuItem(items[itemIndex].Label.c_str()) && OnReviewMenuItem)
			{
				OnReviewMenuItem(items[itemIndex]);
			}

			ImGui::PopID();
		}
	}

	void GitView::RequestLog(const HistoryReach& reach, LogAsk ask)
	{
		m_Reach = reach;

		// WARNING: Accept tells the settling filter that this query already carries what is typed,
		// or its wait would run out afterwards and send the same query a second time.
		m_LogRequested = true;
		m_Filter.Accept();

		// WARNING: Asked before the line read is in, the page would land without the fold row and the dimmed rows the
		// line gives it, and they would appear in it later. AskLogAsNeeded asks it once the read is in.
		m_LogHeld = WaitsForLines();

		if (m_LogHeld)
		{
			return;
		}

		m_LineAsked = ShownRefLine();
		const HistoryLine line = LineToShow();

		Core::LogRequest log = BuildLogRequest(m_SelectedRef, m_Filter.Get(), m_Reach, m_SelectedStash, m_FirstParentsOnly);
		WidenToLine(log, line);

		std::optional<GitModel::MarkedCommitsRequest> marked;

		if (const std::optional<Core::LogRequest> markedQuery = MarkedQueryFor(line, log))
		{
			const GitModel::MarkedKind kind =
				line.Fold == LineFold::Folded ? GitModel::MarkedKind::Own : GitModel::MarkedKind::Newer;
			marked = GitModel::MarkedCommitsRequest{.Query = *markedQuery, .Label = line.LineName, .Kind = kind};
		}

		m_Model.RequestLog(std::move(log), std::move(marked));

		// Asked with the log, so the rows that wait for the fold row's count wait as little as possible.
		if (line.Fold == LineFold::Folded)
		{
			static_cast<void>(m_Lines.DistanceOf(line.BranchTip, line.LineTip));
		}

		if (ask == LogAsk::ByReader)
		{
			m_AskedLogGeneration = m_Model.GetLogGeneration();
		}
	}

	void GitView::AskLogAsNeeded()
	{
		// Nothing is asked before it is known which history the window opens on.
		if (RestoreWaits())
		{
			return;
		}

		if (m_LogHeld)
		{
			if (!WaitsForLines())
			{
				RequestLog(m_Reach);
			}

			return;
		}

		if (!m_LogRequested)
		{
			// The window's first history is opened as a reference picked in it is.
			if (!m_Model.GetLog())
			{
				StartOpening();
				RequestLog(HistoryReach{});
			}

			return;
		}

		// A new read that moved the shown reference's line, its tip or the reference's own tip asks the page and its
		// marks again, so they keep to what the header says.
		if (!WaitsForLines() && ShownRefLine() != m_LineAsked)
		{
			RequestLog(m_Reach, LogAsk::Again);
		}
	}

	const BranchLines* GitView::CurrentLines() const
	{
		return m_SidebarLines.has_value() ? &*m_SidebarLines : nullptr;
	}

	HistoryLine GitView::ShownRefLine() const
	{
		const BranchLines* const lines = CurrentLines();
		const std::string shown = ShownRefName();

		if (lines == nullptr || shown.empty())
		{
			return {};
		}

		return HistoryLineOf(*m_SidebarBuiltFrom->Read, *lines, shown, m_NewerShown.contains(shown));
	}

	HistoryLine GitView::LineToShow() const
	{
		return IsFileHistory() ? HistoryLine{} : ShownRefLine();
	}

	bool GitView::IsFileHistory() const
	{
		return !Core::Trimmed(m_Filter.Get().Path).empty();
	}

	bool GitView::WaitsForLines() const
	{
		if (m_SelectedStash.has_value() || IsFileHistory())
		{
			return false;
		}

		const BranchLines* const lines = CurrentLines();

		if (lines == nullptr)
		{
			// Neither a first read that failed nor one waiting for branches that could not be listed is coming.
			const bool readFailed = m_Lines.GetError().has_value() && !m_Lines.IsReading();
			const bool branchesFailed = !m_Model.GetBranches() && m_Model.GetBranchesError().has_value();

			return !readFailed && !branchesFailed;
		}

		// A reference the read does not hold yet, one just made or renamed, is waited for while a read is on its way.
		const bool readLacksSelected = m_SelectedRef.has_value() && !lines->IndexOf(*m_SelectedRef).has_value();

		return readLacksSelected && m_Lines.IsReading();
	}

	bool GitView::ShowsLocalChanges() const
	{
		// A file's history is about that file, not about the whole tree's changes; a history still being decided is none.
		if (m_SelectedStash.has_value() || IsFileHistory() || RestoreWaits())
		{
			return false;
		}

		if (!m_SelectedRef.has_value())
		{
			return true;
		}

		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();
		const Core::Branch* const checkedOut = branches ? CheckedOutBranch(*branches) : nullptr;

		return checkedOut != nullptr && checkedOut->FullName == *m_SelectedRef;
	}

	std::optional<std::string> GitView::LocalChangesMark() const
	{
		if (!ShowsLocalChanges())
		{
			return std::nullopt;
		}

		return DescribeLocalChanges(m_Model.GetStatus().get());
	}

	bool GitView::PageAnswersSelection(const Core::LogRequest& shown) const
	{
		Core::LogRequest selected;
		selected.Ref = m_SelectedRef;
		selected.Stash = m_SelectedStash;

		return !AnswersAnotherHistory(shown, selected);
	}

	bool GitView::ShowsCommitRows()
	{
		if (m_LogHeld || WaitsForLines() || !m_Model.GetLog())
		{
			return false;
		}

		// WARNING: The page in hand may be another history's, kept while the one now selected is read or held back;
		// its rows would stand under this history's name.
		const std::optional<Core::LogRequest>& shown = m_Model.GetShownLogRequest();

		if (!shown.has_value() || !PageAnswersSelection(*shown))
		{
			return false;
		}

		// Rows once on screen stay; only the first time are they held for the fold row's count.
		if (m_RowsShownFor == SelectedHistory{.Ref = m_SelectedRef, .Stash = m_SelectedStash})
		{
			return true;
		}

		const HistoryLine line = LineToShow();
		std::optional<std::size_t> behind;

		if (line.Fold == LineFold::Folded)
		{
			if (const std::optional<Core::LineDistance> distance = m_Lines.DistanceOf(line.BranchTip, line.LineTip))
			{
				behind = distance->Behind;
			}
		}

		return !RowsWaitForFoldCount(line, *shown, behind);
	}

	const std::unordered_set<Core::Oid>& GitView::MarkedCommitsOnScreen()
	{
		const Core::Ref<const Core::LogPage> marked = m_Model.GetMarkedCommits();

		if (marked != m_MarkedOf)
		{
			m_MarkedOf = marked;
			m_Marked.clear();

			if (marked)
			{
				m_Marked.reserve(marked->Commits.size());

				for (const Core::CommitSummary& commit : marked->Commits)
				{
					m_Marked.insert(commit.Id);
				}
			}
		}

		return m_Marked;
	}

	HistoryMarks GitView::HistoryMarksFor(const Core::Ref<const Core::LogPage>& page)
	{
		HistoryMarks marks;
		marks.LocalChanges = LocalChangesMark();

		// WARNING: The marks are read from what landed with the page, not from the line now, so the rows they give
		// change only when a page asked with the new line lands.
		const Core::Ref<const Core::LogPage> marked = m_Model.GetMarkedCommits();
		const std::optional<GitModel::MarkedKind> kind = m_Model.GetMarkedKind();

		if (!marked || !kind.has_value())
		{
			return marks;
		}

		const HistoryLine line = LineToShow();

		if (*kind == GitModel::MarkedKind::Newer)
		{
			marks.Dimmed = &MarkedCommitsOnScreen();

			if (line.Fold == LineFold::Shown)
			{
				marks.FoldFooter = m_Model.GetMarkedLabel() + "'s newer commits shown, dimmed \xC2\xB7 ";
			}

			return marks;
		}

		std::optional<std::size_t> behind;

		if (line.Fold != LineFold::None)
		{
			if (const std::optional<Core::LineDistance> distance = m_Lines.DistanceOf(line.BranchTip, line.LineTip))
			{
				behind = distance->Behind;
			}
		}

		FoldSource source{.Page = page, .Own = marked, .Line = m_Model.GetMarkedLabel(), .Behind = behind};

		// A count asked again, as for a line that moved, leaves the fold row drawn for this page as it was until it lands.
		const bool keepsDrawnRow =
			!behind.has_value() && m_FoldOf.has_value() && m_FoldOf->Page == page && m_FoldOf->Own == marked;

		if (!keepsDrawnRow && m_FoldOf != source)
		{
			m_Fold = FindFoldRow(*page, *marked, source.Line, behind);
			m_FoldOf = std::move(source);
		}

		marks.Fold = m_Fold;
		return marks;
	}

	void GitView::ToggleNewerShown()
	{
		const std::string shown = ShownRefName();

		if (shown.empty())
		{
			return;
		}

		if (m_NewerShown.erase(shown) == 0)
		{
			m_NewerShown.insert(shown);
		}

		RequestLog(m_Reach);
	}

	void GitView::AnswerListClicks()
	{
		// The reader's own choice wins over the row an opening would pick.
		if (m_Commits.TakeLocalChangesClick())
		{
			m_OpeningRef = false;
		}

		if (m_Commits.TakeFoldClick())
		{
			ToggleNewerShown();
		}
	}

	void GitView::SelectOpenedStart()
	{
		if (!m_OpeningRef || m_LogHeld || !m_Model.IsLogAnswered(m_AskedLogGeneration))
		{
			return;
		}

		// An ask answered without a page of this history failed: there is nothing to start on.
		const std::optional<Core::LogRequest>& shown = m_Model.GetShownLogRequest();

		if (!m_Model.GetLog() || !shown.has_value() || !PageAnswersSelection(*shown))
		{
			m_OpeningRef = false;
			return;
		}

		// The rows may still wait for the fold row's count; the opening waits with them, and then happens once only:
		// a page that lands later never moves the list.
		if (!ShowsCommitRows())
		{
			return;
		}

		m_OpeningRef = false;

		const std::optional<GitModel::MarkedKind> kind = m_Model.GetMarkedKind();
		const std::unordered_set<Core::Oid>& marked = MarkedCommitsOnScreen();
		const std::unordered_set<Core::Oid>* const own = kind == GitModel::MarkedKind::Own ? &marked : nullptr;
		const std::unordered_set<Core::Oid>* const newer = kind == GitModel::MarkedKind::Newer ? &marked : nullptr;

		if (const std::optional<Core::Oid> start = StartingCommit(*m_Model.GetLog(), own, newer))
		{
			m_Commits.SelectCommit(*start);
			m_Commits.ScrollToCommit(*start);
		}
	}

	void GitView::UpdateForNewPage()
	{
		const Core::Ref<const Core::LogPage> page = m_Model.GetLog();

		if (page != m_LastSeenPage)
		{
			m_LastSeenPage = page;

			if (page)
			{
				m_Commits.KeepSelectionWithin(*page, BroughtMarks());
				AdvanceReveal(*page);
			}
			else
			{
				m_Commits.ClearSelection();
			}
		}

		SelectOpenedStart();
		ConfirmRevealedFile();
	}

	float GitView::FilterFieldsBasis()
	{
		return WindowBarEm(kFilterFieldPixels) + ImGui::GetFontSize() * kPathBoxWidthInFontSizes + ImGui::GetStyle().ItemSpacing.x;
	}

	void GitView::RenderFilterFields(float room)
	{
		// Short of room, the path field gives up its share as the filter field does.
		const float fullPathWidth = ImGui::GetFontSize() * kPathBoxWidthInFontSizes;
		const FilterFieldWidths widths =
			SplitFilterFields(room, fullPathWidth, FilterFieldsBasis(), ImGui::GetStyle().ItemSpacing.x);
		const float pathWidth = widths.Path;
		const float wordsWidth = widths.Words;

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

		const auto diffOf = [this](const Core::Oid& commit) {
			return m_Model.GetDiffFor(commit);
		};

		const auto diffFailed = [this](const Core::Oid& commit) { return m_Model.IsDiffFailed(commit); };

		m_Commits.SetActionsEnabled(!m_Model.IsWriteUnsettled());
		m_Commits.SetLocalChangesInk(ReadEditorPalette(m_Scheme.Get()).DiagnosticWarning);

		if (!ShowsCommitRows())
		{
			// The Local changes row stands above the wait, as it will above the commits.
			HistoryMarks marks;
			marks.LocalChanges = LocalChangesMark();

			const LogAttempt attempt =
				m_Model.IsLogAnswered(m_Model.GetLogGeneration()) ? LogAttempt::Settled : LogAttempt::InFlight;
			const std::optional<std::string> placeholder = HistoryPlaceholder(
				m_Model.GetStatus() != nullptr, m_Model.GetStatusError(), m_Model.GetLogError(), attempt);

			// A page of this history that waits for its fold row's count is read already; the wait is the line's.
			const std::optional<Core::LogRequest>& shown = m_Model.GetShownLogRequest();
			const bool pageWaitsForCount = page && shown.has_value() && PageAnswersSelection(*shown);

			if (m_LogHeld || WaitsForLines() || pageWaitsForCount || RestoreWaits())
			{
				marks.Waiting = "Reading the branches' lines\xE2\x80\xA6";
			}
			else if (placeholder.has_value())
			{
				m_Commits.SetEmptyText(*placeholder);
			}
			else
			{
				marks.Waiting = "Loading history\xE2\x80\xA6";
			}

			const Core::LogPage noCommits;
			const Core::CommitGraph noGraph;

			m_Commits.SetRoomBelow(0.0f);
			m_Commits.Render(noCommits, noGraph, palette, ReaderTimeZone(), diffOf, diffFailed, m_CommitRefs, {}, {},
							 marks);

			AnswerListClicks();
		}
		else
		{
			const std::optional<Core::LogRequest>& shownRequest = m_Model.GetShownLogRequest();
			const Core::LayoutParents parents = shownRequest.has_value() && shownRequest->FirstParentOnly
													? Core::LayoutParents::FirstOnly
													: Core::LayoutParents::All;

			if (page != m_GraphOf || parents != m_GraphParents)
			{
				m_GraphOf = page;
				m_GraphParents = parents;
				m_Graph = Core::LayoutCommits(page->Commits, parents);
			}

			const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

			if (branches != m_RefsOf)
			{
				m_RefsOf = branches;
				m_CommitRefs = BuildCommitRefs(branches ? *branches : std::vector<Core::Branch>{});
			}

			m_RowsShownFor = SelectedHistory{.Ref = m_SelectedRef, .Stash = m_SelectedStash};

			HistoryMarks marks = HistoryMarksFor(page);
			const HistoryMarks broughtMarks = BroughtMarks();
			marks.BroughtOf = broughtMarks.BroughtOf;
			marks.BroughtFailure = broughtMarks.BroughtFailure;
			AskBroughtByOpenMerges(*page, broughtMarks);

			std::optional<HistoryStatus> status = DescribeHistory(HistoryStatusInputFor(*page));

			// The offer to fold the line's newer commits again needs the line even when the page has nothing to count.
			if (!status.has_value() && !marks.FoldFooter.empty())
			{
				status = HistoryStatus{};
			}

			m_Commits.SetRoomBelow(status.has_value() ? HistoryStatusLineHeight() : 0.0f);
			m_Commits.SetEmptyText(IsShownLogNarrowed() ? "No matching commits." : "No commits yet.");

			m_Commits.Render(*page, m_Graph, palette, ReaderTimeZone(), diffOf, diffFailed, m_CommitRefs,
							 m_Filter.Get().Text, m_Filter.Get().Path, marks);

			// WARNING: Drawn straight after the list, before anything else moves the cursor, so it sits
			// in the room the list left for it.
			std::optional<HistoryClick> click;

			if (status.has_value())
			{
				click = DrawHistoryStatusLine(*status, marks.FoldFooter);
			}

			ApplyCommitAction();
			AnswerListClicks();

			std::unordered_set<Core::Oid> wanted;

			const std::optional<Core::Oid> selectedCommit = m_Commits.GetSelectedCommit();

			for (const Core::CommitSummary& commit : page->Commits)
			{
				// An opened merge of the Merges history lists what it brought, not its files.
				const bool listsFiles = m_Commits.IsExpanded(commit.Id) && !(marks.BroughtOf && IsMerge(commit));
				const bool isWanted = listsFiles || selectedCommit == commit.Id;

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

			// A selected commit a merge brought shows its details like any other.
			if (selectedCommit.has_value() && !wanted.contains(*selectedCommit) && FindShownCommit(*page, *selectedCommit, broughtMarks))
			{
				wanted.insert(*selectedCommit);

				if (!m_Model.GetDiffFor(*selectedCommit))
				{
					m_Model.RequestDiffFor(*selectedCommit);
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

		// WARNING: The count is of the reference alone, so a page that also walks its line's newer commits is not
		// said to be a part of it.
		const bool countsThePage = !shown.has_value() || shown->AlsoFrom.empty();

		return HistoryStatusInput{.Shown = page.Commits.size(),
								  .Complete = page.Complete,
								  .Filtered = IsShownLogNarrowed(),
								  .Stash = shown.has_value() && shown->Stash.has_value(),
								  .Total = countsThePage ? m_Model.GetCommitCountFor(shown.has_value() ? shown->Ref : std::nullopt,
																					  shown.has_value() && shown->FirstParentOnly
																						  ? Core::FollowedParents::FirstOnly
																						  : Core::FollowedParents::All)
														 : std::nullopt,
								  .Counting = countsThePage && m_Model.IsCommitCountInFlight(),
								  .FirstParentsOnly = shown.has_value() && shown->FirstParentOnly,
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
		if (click == HistoryClick::Fold)
		{
			ToggleNewerShown();
			return;
		}

		const std::optional<Core::LogRequest>& shown = m_Model.GetShownLogRequest();

		if (!shown.has_value())
		{
			return;
		}

		RequestLog(ReachAfterClick(click == HistoryClick::All, NarrowsHistory(*shown),
								   HistoryReach{.MaxCount = shown->MaxCount, .MaxWalk = shown->MaxWalk},
								   page.Commits.size(), page.Walked));
	}

	HistoryMarks GitView::BroughtMarks() const
	{
		HistoryMarks marks;
		const std::optional<Core::LogRequest>& shown = m_Model.GetShownLogRequest();

		if (!shown.has_value() || !shown->FirstParentOnly)
		{
			return marks;
		}

		marks.BroughtOf = [this](const Core::Oid& merge) { return m_Model.GetBrought(merge); };
		marks.BroughtFailure = [this](const Core::Oid& merge) -> std::optional<std::string> {
			const std::optional<Core::Error> failure = m_Model.GetBroughtFailure(merge);

			if (!failure.has_value())
			{
				return std::nullopt;
			}

			return failure->Message;
		};

		return marks;
	}

	void GitView::AskBroughtByOpenMerges(const Core::LogPage& page, const HistoryMarks& brought)
	{
		if (!brought.BroughtOf)
		{
			return;
		}

		// WARNING: Asked each frame: a page already held or on its way is not asked again, and one the cache let go of
		// is read again.
		for (const Core::CommitSummary& commit : page.Commits)
		{
			if (!IsMerge(commit))
			{
				continue;
			}

			// Closing a merge forgets a failed read, so opening it again asks again, as a diff does.
			if (m_Commits.IsExpanded(commit.Id) || m_Commits.GetSelectedCommit() == commit.Id)
			{
				m_Model.RequestBrought(commit.Id, commit.Parents[0], commit.Parents[1]);
			}
			else
			{
				m_Model.ForgetFailedBrought(commit.Id);
			}
		}
	}

	std::optional<Core::CommitSummary> GitView::FindShownCommit(const Core::LogPage& page, const Core::Oid& id,
																const HistoryMarks& broughtMarks) const
	{
		for (const Core::CommitSummary& commit : page.Commits)
		{
			if (commit.Id == id)
			{
				return commit;
			}
		}

		if (!broughtMarks.BroughtOf)
		{
			return std::nullopt;
		}

		for (const Core::CommitSummary& merge : page.Commits)
		{
			if (!IsMerge(merge) || !m_Commits.IsExpanded(merge.Id))
			{
				continue;
			}

			const Core::Ref<const Core::LogPage> brought = m_Model.GetBrought(merge.Id);

			if (!brought)
			{
				continue;
			}

			for (const Core::CommitSummary& commit : brought->Commits)
			{
				if (commit.Id == id)
				{
					return commit;
				}
			}
		}

		return std::nullopt;
	}

	void GitView::RenderPreview(const GitPalette& palette)
	{
		if (m_Commits.IsLocalChangesSelected())
		{
			RenderChanges(palette);
			return;
		}

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

		if (const std::optional<Core::CommitSummary> summary = FindShownCommit(*page, *commit, BroughtMarks()))
		{
			RenderDetails(*summary, palette);
			return;
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

		const bool diffFailed = m_Model.IsDiffFailed(commit.Id);

		CommitDetailsContent content;

		// Without a read there is no answer to wait for or to give; the line shows its ellipsis.
		const Core::Ref<const LineRead> read = m_Lines.GetRead();
		content.Bases = read ? DescribeInBases(*read, m_Lines.BasesHolding(commit.Id)) : InBasesWithoutRead(m_Lines.IsReading(), m_Lines.GetError());

		if (const auto refs = m_CommitRefs.find(commit.Id); refs != m_CommitRefs.end())
		{
			content.Refs = refs->second;
		}

		if (IsMerge(commit))
		{
			m_Model.RequestBrought(commit.Id, commit.Parents[0], commit.Parents[1]);

			BroughtDetails brought;
			brought.Page = m_Model.GetBrought(commit.Id);

			if (const std::optional<Core::Error> failure = m_Model.GetBroughtFailure(commit.Id))
			{
				brought.Failure = failure->Message;
			}

			content.Brought = std::move(brought);
		}

		const CommitDetailsOutcome outcome =
			Graphite::RenderCommitDetails(commit, stats, diffFailed, ReaderTimeZone(), palette, content);

		if (outcome.Closed)
		{
			m_Commits.ClearRowSelection();
		}
		else if (outcome.ChosenBrought.has_value())
		{
			// The merge's brought commits are findable for the details only while it is open in the history.
			if (BroughtMarks().BroughtOf)
			{
				m_Commits.SetExpanded(commit.Id, true);
			}

			m_Commits.SelectCommit(*outcome.ChosenBrought);
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

		if (const std::optional<std::string> failure = DiffFailureOf(selection, m_Model); !diff && failure.has_value())
		{
			if (Core::EmptyState(*failure, "Retry"))
			{
				RetryDiff(selection, m_Model);
			}

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
		m_Diff.SetTextsWait(m_Texts.Wait);

		const SyntaxPalette syntax = ReadSyntaxPalette(m_Scheme.Get());
		m_Diff.Render(file, palette, syntax, ReadEditorPalette(m_Scheme.Get()).Selection, m_Texts.Texts,
					  m_Texts.Tokens, m_EditorSettings.GetDiffViewOptions());
	}
}
