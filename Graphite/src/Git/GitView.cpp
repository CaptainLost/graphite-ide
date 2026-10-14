#include "Git/GitView.h"

#include "Core/Base/Base.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/ContextMenu.h"
#include "Core/ImGui/EmptyState.h"

#include "Theme/Palettes.h"
#include "Theme/GitPalette.h"
#include "UI/ClipboardCopy.h"
#include "Git/History/HistoryFilter.h"
#include "Git/History/HistoryPlaceholder.h"
#include "Git/InitRepositoryPrompt.h"
#include "UI/HighlightedRow.h"
#include "UI/SplitResults.h"
#include "UI/ToastEvent.h"
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

		constexpr float kRefsColumnWidthInFontSizes = 10.0f;
		constexpr float kPathBoxWidthInFontSizes = 14.0f;
		constexpr float kFillAvailableWidth = -1.0f;
		constexpr float kCalcTextSizeRoundingPixel = 1.0f;
		constexpr int kGitTableColumnCount = 2;
		constexpr int kRefsColumn = 0;
		constexpr int kHistoryColumn = 1;
		constexpr std::size_t kLoadMoreGrowthFactor = 2;

		std::filesystem::path RelativeOrClimbing(const std::filesystem::path& path, const std::filesystem::path& root)
		{
			return Core::RelativeTo(path, root).value_or(path.lexically_relative(root));
		}

		bool ContainsBranch(const std::vector<Core::Branch>& branches, const std::string& fullName)
		{
			for (const Core::Branch& branch : branches)
			{
				if (branch.FullName == fullName)
				{
					return true;
				}
			}

			return false;
		}

		bool IsListedInGroup(const Core::Branch& branch, Core::RefKind kind, const std::string& refFilter)
		{
			return branch.Kind == kind && MatchesRefFilter(branch.FullName, refFilter);
		}

		std::size_t CountListedBranches(const std::vector<Core::Branch>& branches, Core::RefKind kind,
										const std::string& refFilter)
		{
			std::size_t count = 0;

			for (const Core::Branch& branch : branches)
			{
				if (IsListedInGroup(branch, kind, refFilter))
				{
					++count;
				}
			}

			return count;
		}

		float WidthOfPiece(std::string_view piece)
		{
			// WARNING: An empty view may carry no pointer at all, and CalcTextSize reads a null end as
			// "up to the terminator".
			return piece.empty() ? 0.0f : ImGui::CalcTextSize(piece.data(), piece.data() + piece.size()).x;
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

		const bool isAheadOrBehindUpstream = branch.DistanceKnown && (branch.Ahead > 0 || branch.Behind > 0);

		if (isAheadOrBehindUpstream)
		{
			label += std::format("  {}/{}", branch.Ahead, branch.Behind);
		}

		return label;
	}

	GitView::GitView(GitModel& model, const ActiveScheme& scheme, EditorSettings& editorSettings,
					 DiffWindows& diffWindows, ModalGate& modals, EditorBuffers& buffers,
					 Core::EventBus& events)
		: Core::Panel("git", "Git", "git", ICON_MDI_SOURCE_BRANCH, StartsAs::Hidden), m_Model(model),
		  m_Scheme(scheme), m_EditorSettings(editorSettings), m_DiffWindows(diffWindows),
		  m_Buffers(buffers), m_Events(events), m_DetachQuestion(modals, "###GitDetach"),
		  m_CheckoutQuestion(modals, "###GitCheckout"),
		  m_NewBranchQuestion(modals, "###GitNewBranch"), m_RenameBranchQuestion(modals, "###GitRenameBranch"),
		  m_DeleteBranchQuestion(modals, "###GitDeleteBranch"), m_DropStashQuestion(modals, "###GitDropStash")
	{
		m_Diff.OnSetWordWrap = [this](bool wrap) { m_EditorSettings.SetDiffWordWrap(wrap); };
	}

	void GitView::OnUpdate(Core::Timestep)
	{
		// WARNING: Begin hands the modal gate back, and it only runs from OnImGuiRender, which is
		// skipped for a panel that is hidden or behind another tab. An undrawn panel withdraws its
		// questions here or it holds the gate for good.
		if (!HasBeenDrawnOnLatestFrame())
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
			const bool isAwaitedCheck = m_PendingDelete == check->Branch;

			if (isAwaitedCheck)
			{
				m_PendingDelete.reset();

				if (check->Failure.has_value())
				{
					ShowFailure(m_Events, "Delete branch failed", check->Branch + ": " + check->Failure->Message,
								m_Model.GetWorkingDirectory());
				}
				else
				{
					AskOnce(m_DeleteBranchQuestion,
							DeleteBranchPayload{.Branch = check->Branch, .Unmerged = check->Unmerged});
				}
			}
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

		if (ContainsBranch(*branches, *m_SelectedRef))
		{
			return;
		}

		const bool isFollowingRename = m_PendingRename.has_value() && m_PendingRename->From == *m_SelectedRef &&
									   ContainsBranch(*branches, m_PendingRename->To);

		if (isFollowingRename)
		{
			m_SelectedRef = m_PendingRename->To;
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
	}

	void GitView::BeginCheckout(const Core::CheckoutTarget& target)
	{
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
			const std::string failureMessage =
				answer.Failure.has_value() ? answer.Failure->Message : std::string("no plan");
			ShowFailure(m_Events, "Checkout failed", label + ": " + failureMessage, m_Model.GetWorkingDirectory());
			return;
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
			return;
		}

		const Core::Ref<const Core::WorkingTreeStatus> gitStatusIfKnown = m_Model.GetStatus();

		CheckoutQuestion question = BuildCheckoutQuestion(
			label, *answer.Plan, gitStatusIfKnown ? *gitStatusIfKnown : Core::WorkingTreeStatus{}, unsavedRelative);

		const bool untrackedForced = question.UntrackedForced;

		AskOnce(m_CheckoutQuestion, CheckoutQuestionPayload{.Target = std::move(answer.Target),
															.Question = std::move(question),
															.UnsavedAbsolute = std::move(unsaved),
															.IncludeUntracked = untrackedForced});
	}

	void GitView::BeginNewBranch(std::string from, const Core::Oid& at, std::string_view suggested)
	{
		BranchNamePayload payload{.Mode = BranchNameMode::New, .From = std::move(from), .At = at};

		const std::size_t copiedLength = std::min(suggested.size(), payload.Buffer.size() - 1);
		std::copy_n(suggested.begin(), copiedLength, payload.Buffer.begin());

		AskOnce(m_NewBranchQuestion, std::move(payload));
	}

	void GitView::BeginRename(const std::string& branch)
	{
		BranchNamePayload payload{.Mode = BranchNameMode::Rename, .From = branch};

		const std::size_t copiedLength = std::min(branch.size(), payload.Buffer.size() - 1);
		std::copy_n(branch.begin(), copiedLength, payload.Buffer.begin());

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
		{
			const Core::CheckoutTarget target{.Kind = Core::CheckoutKind::Commit, .Commit = action->Commit};

			if (AsksBeforeDetaching(target))
			{
				AskOnce(m_DetachQuestion, DetachPayload{.Commit = action->Commit});
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
		m_Commits.Reset();

		m_FilterBuffer = {};
		m_FilterPathBuffer = {};
		m_RefFilterBuffer = {};
		m_Filter.Reset();

		m_SelectedRef.reset();
		m_SelectedStash.reset();
		m_LastSeenBranches.reset();
		m_PendingRename.reset();

		m_PageWhenAsked.reset();
		m_LastSeenPage.reset();
		m_Searching = false;

		// WARNING: Withdraw is only valid outside a popup, and this runs from an event handler.
		WithdrawQuestions();
		m_PendingCheckout.reset();
		m_PendingDelete.reset();
	}

	void GitView::OnImGuiRender()
	{
		UpdateForNewPage();

		if (IsMissingRepository(m_Model.GetStatus() != nullptr, m_Model.GetStatusError()))
		{
			// WARNING: The dialogs below are not drawn on this path, and Begin is what hands the gate
			// back, so a question left standing would hold it for good.
			WithdrawQuestions();
			DrawInitRepositoryPrompt(m_Model);
			return;
		}

		const GitPalette palette = ReadGitPalette(m_Scheme.Get());

		const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

		if (ImGui::BeginTable("git", kGitTableColumnCount, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("refs", ImGuiTableColumnFlags_WidthFixed,
									ImGui::GetFontSize() * kRefsColumnWidthInFontSizes);
			ImGui::TableSetupColumn("history", ImGuiTableColumnFlags_WidthStretch);

			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(kRefsColumn);
			RenderRefs(branches ? *branches : std::vector<Core::Branch>{});

			ImGui::TableSetColumnIndex(kHistoryColumn);

			RenderFilterBar();

			if (const Core::Ref<const Core::LogPage> page = m_Model.GetLog();
				page && m_Filter.Get() != HistoryFilter{})
			{
				RenderFilterSummary(*page);
			}

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

	void GitView::RenderRefs(const std::vector<Core::Branch>& branches)
	{
		ImGui::SetNextItemWidth(kFillAvailableWidth);
		ImGui::InputTextWithHint("##refFilter", ICON_MDI_MAGNIFY "  Filter branches",
								 m_RefFilterBuffer.data(), m_RefFilterBuffer.size());

		const std::string refFilter(m_RefFilterBuffer.data());

		RenderRefGroup(branches, refFilter, "Local", Core::RefKind::Local, RefSection::Local);
		RenderRefGroup(branches, refFilter, "Remote", Core::RefKind::Remote, RefSection::Remote);

		RenderStashes();
	}

	void GitView::RenderRefGroup(const std::vector<Core::Branch>& branches, const std::string& refFilter,
								 const char* heading, Core::RefKind kind, RefSection section)
	{
		const std::size_t count = CountListedBranches(branches, kind, refFilter);

		if (!RenderRefSection(heading, count, section))
		{
			return;
		}

		for (const Core::Branch& branch : branches)
		{
			if (!IsListedInGroup(branch, kind, refFilter))
			{
				continue;
			}

			const bool selected = !m_SelectedStash.has_value() && m_SelectedRef.has_value() &&
								  *m_SelectedRef == branch.FullName;

			ImGui::PushID(branch.FullName.c_str());

			// WARNING: The group makes the menu below ask about the whole row. With the filter
			// marked, HighlightedSelectable ends on a piece of its label, and after EndGroup the
			// menu would ask about the badge.
			ImGui::BeginGroup();

			const std::string label = RefLabel(branch);

			std::string badge;

			const bool isRowOnScreen =
				ImGui::IsRectVisible(ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));

			if (ReviewBadgeOf && isRowOnScreen)
			{
				badge = ReviewBadgeOf(branch);
			}

			const float rowWidth = ImGui::GetContentRegionAvail().x;
			const float badgeWidth = badge.empty() ? 0.0f : ImGui::CalcTextSize(badge.c_str()).x;
			const float labelRoom =
				badge.empty() ? rowWidth : rowWidth - badgeWidth - ImGui::GetStyle().ItemSpacing.x;
			const ImVec2 rowStart = ImGui::GetCursorScreenPos();

			bool cut = false;
			const std::string fitted = FitToWidth(label, labelRoom, &cut);

			const bool clicked = HighlightedSelectable("##ref", selected, fitted, refFilter);

			if (!badge.empty())
			{
				ImGui::SetCursorScreenPos(ImVec2(rowStart.x + rowWidth - badgeWidth, rowStart.y));
				ImGui::TextDisabled("%s", badge.c_str());
			}

			ImGui::EndGroup();

			if (cut)
			{
				const std::string whole = badge.empty() ? label : std::format("{}  {}", label, badge);
				ImGui::SetItemTooltip("%s", whole.c_str());
			}

			RenderRefMenu(branch);

			ImGui::PopID();

			if (clicked)
			{
				m_SelectedRef = branch.FullName;

				DeselectStash();

				RequestLog(std::nullopt);
			}
		}
	}

	bool GitView::RenderRefSection(const char* heading, std::size_t count, RefSection section)
	{
		bool& collapsed = m_CollapsedRefSections[static_cast<std::size_t>(section)];

		ImGui::PushID(heading);

		// WARNING: SeparatorText insets its label by its own padding, so the chevron is lowered by
		// that padding to share the baseline. The separator's horizontal padding is zeroed, or a stub
		// of line shows between the chevron and the name.
		const float padding = ImGui::GetStyle().SeparatorTextPadding.y;
		const float paddingAboveAndBelow = padding * 2.0f;
		const ImVec2 rowStart = ImGui::GetCursorPos();
		const bool notInitiallySelected = false;

		if (ImGui::Selectable("##section", notInitiallySelected, ImGuiSelectableFlags_AllowOverlap,
							  ImVec2(0.0f, ImGui::GetTextLineHeight() + paddingAboveAndBelow)))
		{
			collapsed = !collapsed;
		}

		ImGui::SetCursorPos(ImVec2(rowStart.x, rowStart.y + padding));
		ImGui::TextDisabled("%s", collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN);

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

		const bool notSelected = false;
		const char* const noShortcut = nullptr;

		ImGui::BeginDisabled(m_Model.IsWriteInFlight());

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
				const std::size_t slash = branch.Name.find('/');
				const bool hasRemotePrefix = slash != std::string::npos;
				const std::string_view localName =
					hasRemotePrefix ? std::string_view(branch.Name).substr(slash + 1) : std::string_view{};

				BeginNewBranch(branch.Name, branch.Tip, localName);
			}
		}

		ImGui::EndDisabled();
		RenderReviewItem(branch);
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

	std::string FitToLine(std::string_view text, bool* cut)
	{
		return FitToWidth(text, ImGui::GetContentRegionAvail().x, cut);
	}

	std::string FitToWidth(std::string_view text, float room, bool* cut)
	{
		// WARNING: CalcTextSize rounds widths up, so the room is given back the pixel it rounded
		// away or a prefix that exactly fits is dropped.
		const std::optional<std::size_t> cutPosition =
			CutToFit(text, room + kCalcTextSizeRoundingPixel, WidthOfPiece(Ellipsis), WidthOfPiece);

		if (cut != nullptr)
		{
			*cut = cutPosition.has_value();
		}

		return cutPosition.has_value() ? std::string(text.substr(0, *cutPosition)) + std::string(Ellipsis)
									   : std::string(text);
	}

	std::chrono::minutes GitView::LocalOffset()
	{
		// WARNING: The offset is resolved on every call and never cached, since it changes at a
		// daylight-saving boundary. It is zero when the time zone database is unavailable, as on a
		// Windows install without the ICU data chrono reads, so timestamps fall back to UTC.
		try
		{
			const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

			return std::chrono::floor<std::chrono::minutes>(std::chrono::current_zone()->get_info(now).offset);
		}
		catch (const std::exception&)
		{
			return std::chrono::minutes{0};
		}
	}

	void GitView::RequestLog(std::optional<std::size_t> maxCount)
	{
		m_Model.RequestLog(BuildLogRequest(m_SelectedRef, m_Filter.Get(), maxCount, m_SelectedStash));

		// WARNING: Accept tells the settling filter that this query already carries what is typed,
		// or its wait would run out afterwards and send the same query a second time.
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

		const bool isAnswerToQuery = page != m_PageWhenAsked;
		const bool queryFailed = m_Model.GetLastError().has_value();

		if (isAnswerToQuery || queryFailed)
		{
			m_Searching = false;
			m_PageWhenAsked.reset();
		}
	}

	void GitView::RenderFilterBar()
	{
		const float pathWidth = ImGui::GetFontSize() * kPathBoxWidthInFontSizes;

		ImGui::SetNextItemWidth(-(pathWidth + ImGui::GetStyle().ItemSpacing.x));
		ImGui::InputTextWithHint("##commitFilter",
								 ICON_MDI_MAGNIFY "  Message, author or hash",
								 m_FilterBuffer.data(), m_FilterBuffer.size());

		ImGui::SameLine();
		ImGui::SetNextItemWidth(pathWidth);
		ImGui::InputTextWithHint("##commitPath", ICON_MDI_FILE_OUTLINE "  Main.cpp or *.cpp",
								 m_FilterPathBuffer.data(), m_FilterPathBuffer.size());

		m_Filter.Set(HistoryFilter{std::string(m_FilterBuffer.data()),
								   std::string(m_FilterPathBuffer.data())});

		const bool filterSettled = m_Filter.Advance(Core::Timestep{ImGui::GetIO().DeltaTime});

		if (filterSettled)
		{
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

		ImGui::TextDisabled("%zu matching %s so far — the search stopped before the end of "
							"the history",
							count, count == 1 ? "commit" : "commits");
	}

	void GitView::RenderCommits(const GitPalette& palette)
	{
		const Core::Ref<const Core::LogPage> page = m_Model.GetLog();

		if (!page && !m_LogRequested)
		{
			RequestLog(std::nullopt);
		}

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
			const Core::Ref<const std::vector<Core::Branch>> branches = m_Model.GetBranches();

			if (branches != m_RefsOf)
			{
				m_RefsOf = branches;
				m_CommitRefs = BuildCommitRefs(branches ? *branches : std::vector<Core::Branch>{});
			}

			const auto diffFailed = [this](const Core::Oid& commit) { return m_Model.IsDiffFailed(commit); };

			m_Commits.Render(*page, graph, palette, LocalOffset(), diffOf, diffFailed, m_CommitRefs,
							 m_Filter.Get().Text, m_Filter.Get().Path);

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

			if (m_Commits.TakeLoadMoreRequest())
			{
				RequestLog(page->Commits.size() * kLoadMoreGrowthFactor);
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

		if (!diff)
		{
			Core::EmptyState("Loading…");
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
					  m_Texts.Tokens, m_EditorSettings.IsDiffWordWrap());
	}
}
