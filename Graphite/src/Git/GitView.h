#pragma once

#include "Core/Base/Base.h"

#include "UI/Dialogs/ModalGate.h"
#include "UI/Dialogs/ModalQuestion.h"
#include "Git/CheckoutQuestion.h"
#include "Git/Branches/BranchAuthors.h"
#include "Git/Branches/BranchDeletion.h"
#include "Git/Changes/ChangesView.h"
#include "Git/Diff/DiffBody.h"
#include "Git/History/CommitLabels.h"
#include "Git/History/CommitListView.h"
#include "Git/History/CommitDetailsView.h"
#include "Git/History/CommitReveal.h"
#include "Git/History/ForkSeparator.h"
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
		GitView(GitModel& model, LinesModel& lines, ChangesView& changes, const ActiveScheme& scheme,
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

		void ApplyPendingRestore();

		// The repository, its remotes and the checked-out branch, and the question for the main line when it is asked.
		void RenderWindowBar();

		void RenderSidebar();
		void RebuildSidebarWhenOutOfDate();

		// What a row stands for in the snapshots its rows were built from; null for another kind of row.
		const Core::Branch* SidebarBranchOf(const SidebarRow& row) const;
		const Core::StashEntry* SidebarStashOf(const SidebarRow& row) const;

		SidebarRowText SidebarTextOf(const SidebarRow& row) const;
		SidebarSelection SidebarSelectionNow() const;
		void AnswerSidebarClick(const SidebarRow& row);
		void AnswerSidebarLink(SidebarLink link, const std::string& remote);

		// Fills the menu the sidebar opened for a branch, base or stash row.
		void DrawSidebarRowMenu(const SidebarRow& row);

		// WARNING: Reached from nowhere until the history's Local changes row shows them in its preview.
		void RenderChanges(const GitPalette& palette);
		void RenderChangesDiff(const GitPalette& palette);

		void RenderHistory(const GitPalette& palette);
		void RenderHistoryHeader();

		std::string HistoryTitle() const;

		void FollowSelectedRef();
		void FollowHistoryRef(const std::vector<Core::Branch>& branches, bool renameLanded);
		void FollowSelectedStash();

		// Opens the menu on a right-click of the last item.
		void RenderRefMenu(const Core::Branch& branch);

		// Fills a menu already open.
		void RenderRefMenuItems(const Core::Branch& branch);

		void RenderReviewItem(const Core::Branch& branch);
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

		void RenderDiff(const GitPalette& palette);

		void RenderDetails(const Core::CommitSummary& commit, const GitPalette& palette);

		void RenderFilterBar();

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

		struct BaseSeen
		{
			std::string Branch;
			Core::Oid BaseTip;

			friend bool operator==(const BaseSeen&, const BaseSeen&) = default;
		};

		static std::optional<BaseSeen> BaseOf(const std::optional<ForkBase>& fork);

		std::optional<ForkSeparator> ForkSeparatorFor(const Core::LogPage& page) const;

		// The sidebar's lines of the read on screen; null before the first read lands.
		const BranchLines* CurrentLines() const;

		std::optional<GitModel::OwnCommitsRequest> OwnCommitsToAsk(const std::vector<Core::Branch>& branches);

		void AskAgainWhenBaseMoved(const std::vector<Core::Branch>& branches);

		std::optional<BaseSeen> m_BaseSeen;

		GitModel& m_Model;
		LinesModel& m_Lines;
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
