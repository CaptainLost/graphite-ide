#pragma once

#include "Core/Base/Base.h"

#include "UI/Dialogs/ModalGate.h"
#include "UI/Dialogs/ModalQuestion.h"
#include "Git/CheckoutQuestion.h"
#include "Git/Branches/BranchAuthors.h"
#include "Git/Branches/BranchDeletion.h"
#include "Git/Changes/ChangesView.h"
#include "Git/Compare/VsLabel.h"
#include "Git/Compare/VsPickerView.h"
#include "Git/Diff/DiffBody.h"
#include "Git/History/CommitLabels.h"
#include "Git/History/CommitListView.h"
#include "Git/History/CommitDetailsView.h"
#include "Git/History/CommitReveal.h"
#include "Git/History/FoldRow.h"
#include "Git/Diff/DiffView.h"
#include "Git/Diff/DiffWindows.h"
#include "Editor/EditorSettings.h"
#include "Git/EditorBuffers.h"
#include "Git/GitActivity.h"
#include "Git/GitModel.h"
#include "Review/ReviewMenu.h"
#include "Git/History/HistoryFilter.h"
#include "Git/History/HistoryReach.h"
#include "Git/History/HistoryStatus.h"
#include "Git/History/HistoryStatusLine.h"
#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LinesModel.h"
#include "Git/Sidebar/SidebarRows.h"
#include "Git/Sidebar/SidebarText.h"
#include "Git/Sidebar/SidebarView.h"
#include "Git/Window/WindowBar.h"
#include "Git/Worktrees/WorktreesModel.h"
#include "Persistence/PersistedState.h"
#include "Theme/ActiveScheme.h"

#include "Core/Events/EventBus.h"
#include "Core/Git/Diff.h"
#include "Core/Git/Checkout.h"
#include "Core/Git/GraphLayout.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Stash.h"
#include "Core/UI/Panel.h"

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Graphite {
	std::string RefLabel(const Core::Branch& branch);

	// The Git window: a sidebar of every reference and stash beside the history of the one selected.
	class GitView final : public Core::Panel
	{
	public:
		GitView(GitModel& model, LinesModel& lines, WorktreesModel& worktrees, ChangesView& changes, const ActiveScheme& scheme,
				EditorSettings& editorSettings, DiffWindows& diffWindows, ModalGate& modals, EditorBuffers& buffers,
				Core::EventBus& events);

		void OnUpdate(Core::Timestep timestep) override;

		void OnImGuiRender() override;

		void Subscribe();

		GitPanelWait CurrentWait() const;

		// HEAD's history of one file: the path, relative to the git working directory, goes in the path
		// box, and the text filter, the stash and the selection are let go of.
		void ShowFileHistory(const std::filesystem::path& relativePath);

		// HEAD's whole history, loaded until it holds the commit, which is then selected and expanded
		// with the file selected in it.
		void ShowCommit(const CommitReveal& reveal);

		std::function<ReviewMenu(const Core::Branch& branch)> ReviewMenuOf;

		std::function<void(const ReviewMenuItem& item)> OnReviewMenuItem;

		std::function<void(const std::string& from, const std::string& to)> OnBranchRenamed;

		// The vs button of the history header was clicked.
		std::function<void()> OnVsClicked;

		GitWindowState CaptureWindowState() const;

		// Applied through RefToRestore once the branches are known.
		void RestoreWindowState(const GitWindowState& state);

	private:
		struct CheckoutQuestionPayload
		{
			Core::CheckoutTarget Target;
			CheckoutQuestion Question;

			std::vector<std::filesystem::path> UnsavedAbsolute;

			bool IncludeUntracked = false;

			bool IsSameQuestion(const CheckoutQuestionPayload& other) const { return Target == other.Target; }
		};

		enum class BranchNameMode
		{
			New,
			Rename
		};

		struct BranchNamePayload
		{
			BranchNameMode Mode = BranchNameMode::New;

			std::string From;

			Core::Oid At;

			std::array<char, 256> Buffer{};
			bool CheckoutAfter = true;

			bool IsSameQuestion(const BranchNamePayload& other) const
			{
				return Mode == other.Mode && From == other.From && At == other.At;
			}
		};

		struct DeleteBranchPayload
		{
			std::string Branch;

			bool IsSameQuestion(const DeleteBranchPayload& other) const { return Branch == other.Branch; }

			bool IsAbout(const std::string& branch) const { return Branch == branch; }
		};

		struct DetachPayload
		{
			Core::Oid Commit;

			bool IsSameQuestion(const DetachPayload& other) const { return Commit == other.Commit; }
		};

		struct DropStashPayload
		{
			std::size_t Index = 0;
			Core::Oid Id;

			std::string Message;

			bool IsSameQuestion(const DropStashPayload& other) const { return Id == other.Id; }
		};

		struct BranchRenameSent
		{
			std::string From;
			std::string To;
		};

		void ConnectViews();

		// The history of the ref, or HEAD's with none.
		void SelectRef(std::optional<std::string> fullName);

		// Selects the row the history just opened starts on: Local changes for the checked-out branch's, else the one
		// its first page gives.
		void StartOpening();

		void ApplyPendingRestore();

		// The repository, its remotes and the checked-out branch, and the question for the main line when it is asked.
		void RenderWindowBar();

		void RenderSidebar();
		void RebuildSidebarWhenOutOfDate();

		// What a row stands for in the snapshots its rows were built from; null for another kind of row.
		const Core::Branch* SidebarBranchOf(const SidebarRow& row) const;
		const Core::StashEntry* SidebarStashOf(const SidebarRow& row) const;

		SidebarRowText SidebarTextOf(const SidebarRow& row) const;
		std::optional<bool> IsWorktreeDirty(const std::filesystem::path& worktree) const;
		SidebarSelection SidebarSelectionNow() const;
		void AnswerSidebarClick(const SidebarRow& row);
		void AnswerSidebarLink(SidebarLink link, const std::string& remote);

		// Fills the menu the sidebar opened for a branch, base or stash row.
		void DrawSidebarRowMenu(const SidebarRow& row);

		// The preview of the history's Local changes row: the Changes view's file list beside the diff of the file picked.
		void RenderChanges(const GitPalette& palette);
		void RenderChangesDiff(const GitPalette& palette);

		void RenderHistory(const GitPalette& palette);
		void RenderHistoryHeader();

		std::string HistoryTitle() const;

		// The full name of the branch whose history is shown: the selected ref, or the checked-out branch for HEAD's.
		// Empty for a stash, a detached or unborn HEAD, and while no read is held.
		std::string ShownRefName() const;

		// What the shown reference is compared with and how far from it; asked of the lines model on every frame the
		// header is drawn, which answers from its cache after the first.
		std::optional<Core::LineDistance> ShownDistance(const LineRead& read, std::string_view shownRef);

		void FollowSelectedRef();
		void FollowHistoryRef(const std::vector<Core::Branch>& branches, bool renameLanded);
		void FollowSelectedStash();

		// Opens the menu on a right-click of the last item.
		void RenderRefMenu(const Core::Branch& branch);

		// Fills a menu already open.
		void RenderRefMenuItems(const Core::Branch& branch);

		void RenderReviewItem(const Core::Branch& branch);
		void RenderLineItems(const Core::Branch& branch);
		void RenderWorktreeItem(const Core::Branch& branch);
		void DrawReviewMenuItems(const std::vector<ReviewMenuItem>& items);

		// Fills a menu already open.
		void RenderStashMenuItems(const Core::StashEntry& stash);

		void SelectStash(const Core::Oid& stash);
		void DeselectStash();

		void BeginCheckout(const Core::CheckoutTarget& target);
		void BeginNewBranch(std::string from, const Core::Oid& at, std::string_view suggested = {});
		void BeginRename(const std::string& branch);

		void ApplyCommitAction();

		void ApplyCheckoutPlan(const CheckoutPlanAnswer& answer, bool panelOnScreen);
		void AskCheckoutQuestion(const CheckoutPlanAnswer& answer, std::vector<std::filesystem::path> unsaved,
								 const std::vector<std::filesystem::path>& unsavedRelative);

		void BeginDelete(const std::string& branch);
		void ApplyDeleteCheck(const DeleteCheck& check);
		void FinishDeleteQuestion();

		GitPanelCheckouts Checkouts() const;

		// WARNING: Called before WithdrawQuestions when the panel is not drawn, which would otherwise drop a question
		// the reader has not seen yet.
		void HoldAskedQuestionsForPanel();
		void AskDeleteHeldForPanel();

		void WithdrawQuestions();
		void ForgetRepository();

		void RenderCheckoutDialog();
		void AnswerCheckoutQuestion();
		void RenderDetachDialog();
		void RenderBranchNameDialog(ModalQuestion<BranchNamePayload>& question);
		void RenderDeleteBranchDialog();
		void RenderDropStashDialog();
		void RenderCommits(const GitPalette& palette);
		HistoryStatusInput HistoryStatusInputFor(const Core::LogPage& page) const;
		bool IsShownLogNarrowed() const;
		void AnswerHistoryClick(HistoryClick click, const Core::LogPage& page);
		void RenderPreview(const GitPalette& palette);

		// The Merges history lists what an opened merge brought; empty marks in the other.
		HistoryMarks BroughtMarks() const;

		void AskBroughtByOpenMerges(const Core::LogPage& page, const HistoryMarks& brought);

		// The commit in the page, or among the commits an opened merge of it brought.
		std::optional<Core::CommitSummary> FindShownCommit(const Core::LogPage& page, const Core::Oid& id,
															const HistoryMarks& brought) const;

		void RenderDiff(const GitPalette& palette);

		void RenderDetails(const Core::CommitSummary& commit, const GitPalette& palette);

		// The filter and path fields side by side in `room`, and the filter settling into a new log.
		void RenderFilterFields(float room);

		// The widths the filter and path fields take side by side when there is room: the mockup's 200 px and the path's.
		static float FilterFieldsBasis();

		// The vs button, drawn on the header row within `maxWidth`; true when it was clicked.
		bool DrawVsButton(const VsLabel& label, float maxWidth);

		// The width the vs button takes with the whole name of its line.
		float VsButtonWidth(const VsLabel& label) const;

		// Opens the vs picker for the shown reference, its popup's corner at `below`; nothing when no reference is shown.
		void OpenVsPicker(ImVec2 below);

		// Draws the picker when it is open and sends the change the user chose.
		void DrawVsPicker();

		// The sentence above the list, across the width.
		void DrawHistoryBanner(const std::string& text);

		// The width the Merges / All commits switch takes.
		float HistorySwitchWidth() const;

		// The Merges / All commits switch; true when it changed.
		bool DrawHistorySwitch(bool enabled);

		void UpdateForNewPage();

		void ShowHeadHistory(const std::string& pathText);
		void AdvanceReveal(const Core::LogPage& page);
		void ConfirmRevealedFile();

		enum class LogAsk
		{
			ByReader,
			Again
		};

		void RequestLog(const HistoryReach& reach, LogAsk ask = LogAsk::ByReader);

		// Asks the first log, the log a wait for the line read held back once the read is in, and the log again when
		// the read moved the shown reference's line.
		void AskLogAsNeeded();

		// The sidebar's lines of the read on screen; null before the first read lands.
		const BranchLines* CurrentLines() const;

		// How the shown reference's history shows its line, from the read the sidebar's lines view: the reference alone
		// for a stash, a detached HEAD and while no read is held. The filters are not looked at.
		HistoryLine ShownRefLine() const;

		// The line as the history draws it: none for a file's history, which follows the file alone.
		HistoryLine LineToShow() const;

		// The path box names a file, so the history is that file's.
		bool IsFileHistory() const;

		// A branch's or HEAD's history waits for the line read that holds it; a stash and a file's history do not, and
		// neither does a history whose read failed.
		bool WaitsForLines() const;

		// HEAD's or the checked-out branch's history of the whole tree, whose first row is Local changes.
		bool ShowsLocalChanges() const;

		// What the Local changes row says, for a history that has the row.
		std::optional<std::string> LocalChangesMark() const;

		// The page on screen is the history of the reference or stash selected now.
		bool PageAnswersSelection(const Core::LogRequest& shown) const;

		// The list shows the page's rows rather than a wait; the header's banner comes with them. A folded line's
		// rows wait for the fold row's count the first time the history shows them (asked of the lines model here).
		bool ShowsCommitRows();

		HistoryMarks HistoryMarksFor(const Core::Ref<const Core::LogPage>& page);

		// The marked commits that landed with the page, as a set built once per page of them.
		const std::unordered_set<Core::Oid>& MarkedCommitsOnScreen();

		// "Show them" and "Fold them": the shown branch's line's newer commits beside it, or folded into one row again.
		void ToggleNewerShown();

		void AnswerListClicks();

		// Once the first page of a reference just opened is in, selects the row it starts on and scrolls there.
		void SelectOpenedStart();

		// The shown reference's line as the last log asked of the model was asked with it.
		std::optional<HistoryLine> m_LineAsked;

		// The history waits for the line read, so its log is not asked yet.
		bool m_LogHeld = false;

		// A reference just opened: its first page selects the row it starts on.
		bool m_OpeningRef = false;

		struct SelectedHistory
		{
			std::optional<std::string> Ref;
			std::optional<Core::Oid> Stash;

			friend bool operator==(const SelectedHistory&, const SelectedHistory&) = default;
		};

		// The history whose rows are on screen: they stay, whatever count is asked again for them later.
		std::optional<SelectedHistory> m_RowsShownFor;

		// The branches whose line's newer commits the reader asked to see, by full name, until the folder changes.
		std::set<std::string> m_NewerShown;

		Core::Ref<const Core::LogPage> m_MarkedOf;
		std::unordered_set<Core::Oid> m_Marked;

		struct FoldSource
		{
			Core::Ref<const Core::LogPage> Page;
			Core::Ref<const Core::LogPage> Own;
			std::string Line;
			std::optional<std::size_t> Behind;

			friend bool operator==(const FoldSource&, const FoldSource&) = default;
		};

		// WARNING: Found again only when its source changes: it looks up every own commit of the page.
		std::optional<FoldSource> m_FoldOf;
		std::optional<FoldRow> m_Fold;

		GitModel& m_Model;
		LinesModel& m_Lines;
		WorktreesModel& m_Worktrees;
		ChangesView& m_Changes;
		const ActiveScheme& m_Scheme;

		EditorSettings& m_EditorSettings;

		DiffWindows& m_DiffWindows;

		EditorBuffers& m_Buffers;

		Core::EventBus& m_Events;

		CommitListView m_Commits;
		DiffView m_Diff;

		// A restored history waits here for the branches it names.
		std::optional<GitWindowState> m_PendingRestore;

		struct WindowBarSource
		{
			Core::Ref<const Core::HeadState> Head;
			Core::Ref<const std::vector<Core::Branch>> Branches;
			Core::Ref<const std::vector<Core::Remote>> Remotes;
			bool RemotesFailed = false;
			std::optional<std::string> CheckingOut;
			std::filesystem::path Folder;

			friend bool operator==(const WindowBarSource&, const WindowBarSource&) = default;
		};

		std::optional<WindowBarSource> m_WindowBarSource;
		WindowBarText m_WindowBarText;

		// The last complete bar of this folder, kept while a refresh is incomplete; empty while the first read runs.
		std::optional<WindowBarText> m_WindowBarSettled;

		// "Not now" on the main-line question: until the folder changes.
		bool m_MainLineDismissed = false;

		SidebarView m_Sidebar;

		struct SidebarInputs
		{
			Core::Ref<const LineRead> Read;
			Core::Ref<const std::vector<Core::StashEntry>> Stashes;
			std::optional<std::string> StashesError; // a failed read keeps the list it last had, so it is compared apart
			Core::Ref<const BranchAuthors> Authors;
			std::string Search;

			// Moved by every section or group opened or closed and every remote's merged branches shown or hidden.
			std::uint64_t ChoicesVersion = 0;

			// In whole minutes, so the activity groups are rebuilt once a minute, not each frame.
			std::chrono::sys_seconds Now{};

			friend bool operator==(const SidebarInputs&, const SidebarInputs&) = default;
		};

		// WARNING: The inputs are held, not only compared: the rows' indices point into the read and the stashes held
		// here, m_SidebarLines views into the read, and a snapshot freed and another made at its address must never
		// pass for the one the rows were built from. Without a read the rows are the stashes alone.
		std::optional<SidebarInputs> m_SidebarBuiltFrom;
		std::vector<SidebarRow> m_SidebarRows;
		std::optional<BranchLines> m_SidebarLines;

		// What the shown reference is compared with: opened by the header's vs button and the sidebar's bases link.
		VsPickerView m_VsPicker;
		std::string m_MostCommonLine;
		std::optional<std::size_t> m_HeadRef; // the checked-out branch's ref: its row stands for HEAD's history

		std::map<std::string, bool> m_OpenByUser;
		std::set<std::string> m_ShowingMerged;
		std::uint64_t m_SidebarChoicesVersion = 0;

		// The full name of the branch, or the id of the stash, the open row menu was opened for.
		std::string m_SidebarMenuFor;

		// WARNING: A working-tree diff asks GitModel's single working-tree slot, which a diff tab shares, so it is
		// asked for only while it is drawn.
		DiffBody m_ChangesDiff;
		bool m_ChangesPicked = false;
		bool m_ChangesDrawn = false;

		std::optional<std::string> m_SelectedRef;

		std::optional<Core::Oid> m_SelectedStash;

		Core::Ref<const std::vector<Core::Branch>> m_LastSeenBranches;
		std::optional<BranchRenameSent> m_PendingRename;

		std::array<char, 256> m_FilterBuffer{};
		std::array<char, 260> m_FilterPathBuffer{};

		SettlingFilter m_Filter;

		Core::Ref<const Core::LogPage> m_LastSeenPage;

		// WARNING: Laid out once per page, not once per frame: a page read to its root holds the whole
		// history, and the layout walks every commit of it.
		Core::Ref<const Core::LogPage> m_GraphOf;
		Core::CommitGraph m_Graph;
		Core::LayoutParents m_GraphParents = Core::LayoutParents::All;

		// Merges (first parents only) or All commits; applies to a branch's or HEAD's history.
		bool m_FirstParentsOnly = true;

		float m_ListHeight = 0.0f;

		bool m_LogRequested = false;

		// WARNING: Sent again with every re-ask, so a page the reader loaded further is never cut back
		// to the first one; only a new ref, stash or filter starts it over.
		HistoryReach m_Reach;

		// The log generation of the reader's last ask: the status line reads the reader as waiting
		// until the log has answered it, with a page or an error.
		std::uint64_t m_AskedLogGeneration = 0;

		std::unordered_set<Core::Oid> m_PreviouslyWanted;

		// A commit asked for from outside the panel, waiting for a page that holds it.
		std::optional<CommitReveal> m_PendingReveal;

		// The file a reveal selected, kept until the commit's diff says whether it lists the file.
		std::optional<FileSelection> m_RevealedFile;

		FileTextsBinding m_Texts;

		std::optional<Core::CheckoutTarget> m_PendingCheckout;

		HeldCheckout m_HeldCheckout;
		std::optional<Core::CheckoutTarget> m_AskedCheckout;

		BranchDeletion m_Deletion;

		Core::Ref<const std::vector<Core::Branch>> m_RefsOf;
		CommitRefMap m_CommitRefs;

		ModalQuestion<DetachPayload> m_DetachQuestion;
		ModalQuestion<CheckoutQuestionPayload> m_CheckoutQuestion;
		ModalQuestion<BranchNamePayload> m_NewBranchQuestion;
		ModalQuestion<BranchNamePayload> m_RenameBranchQuestion;
		ModalQuestion<DeleteBranchPayload> m_DeleteBranchQuestion;
		ModalQuestion<DropStashPayload> m_DropStashQuestion;

		bool m_CheckoutDialogOpen = false;
		bool m_IncludeUntracked = false;
		bool m_StashSaveFailed = false;

		const ModalQuestion<BranchNamePayload>* m_BranchNameFor = nullptr;
		std::array<char, 256> m_BranchNameBuffer{};
		bool m_CheckoutAfter = true;
	};
}
