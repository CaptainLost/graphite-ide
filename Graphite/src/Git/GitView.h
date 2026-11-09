#pragma once

#include "Core/Base/Base.h"

#include "UI/Dialogs/ModalGate.h"
#include "UI/Dialogs/ModalQuestion.h"
#include "Git/CheckoutQuestion.h"
#include "Git/Branches/BranchAuthors.h"
#include "Git/Branches/BranchDeletion.h"
#include "Git/Changes/ChangesView.h"
#include "Git/Compare/HistoryBanner.h"
#include "Git/Compare/VsLabel.h"
#include "Git/Compare/VsPickerView.h"
#include "Git/Diff/DiffBody.h"
#include "Git/History/CommitLabels.h"
#include "Git/History/CommitListView.h"
#include "Git/History/CommitDetailsView.h"
#include "Git/History/CommitReveal.h"
#include "Git/History/FoldRow.h"
#include "Git/Content/ContentModel.h"
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
#include "Git/History/ShownHistory.h"
#include "Git/Lines/BranchLines.h"
#include "Git/Lines/HeadInRead.h"
#include "Git/Lines/HeldLines.h"
#include "Git/Lines/LinesModel.h"
#include "Git/Network/NetworkModel.h"
#include "Git/Sidebar/SidebarRows.h"
#include "Git/Sidebar/SidebarText.h"
#include "Git/Sidebar/SidebarView.h"
#include "Git/Window/WindowBar.h"
#include "Git/Worktrees/WorktreeChanges.h"
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
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Graphite {
	std::string RefLabel(const Core::Branch& branch);

	// The Git window: a sidebar of every reference and stash beside the history of the one selected.
	class GitView final : public Core::Panel
	{
	public:
		// `worktreeView` draws `worktreeChanges`' model: another worktree's changes, when the history shows them.
		GitView(GitModel& model, LinesModel& lines, NetworkModel& network, WorktreesModel& worktrees, ContentModel& content, ChangesView& changes,
				WorktreeChanges& worktreeChanges, ChangesView& worktreeView, const ActiveScheme& scheme, EditorSettings& editorSettings, DiffWindows& diffWindows, ModalGate& modals,
				EditorBuffers& buffers, Core::EventBus& events);

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

		// Whether the open folder is trusted, asked each frame, so trusting it enables Fetch, Pull, Push and the banner's
		// Fetch full history at once: git runs the folder's hooks and the commands its config names. Unset counts as
		// trusted, as an unset root permission does for the language server.
		std::function<bool()> IsFolderTrusted;

		GitWindowState CaptureWindowState() const;

		// Applied through RefToRestore once the branches, or for a tag the tags, are known.
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

		// The history of the worktree's HEAD: its branch's for a worktree on a branch, the one from its commit for a
		// detached one, and the open folder's HEAD's for the open folder itself and for a worktree that cannot show its own
		// (gone from the list, its folder missing, its branch unborn).
		void SelectWorktree(const std::filesystem::path& worktree);

		// Selects the row the history just opened starts on: Local changes for the checked-out branch's, else the one
		// its first page gives.
		void StartOpening();

		void ApplyPendingRestore();

		// A restored state still waits for the listing that holds its ref: the history names no reference and shows one
		// waiting row.
		bool RestoreWaits() const { return m_PendingRestore.has_value(); }

		// The repository, its remotes and the checked-out branch, and the question for the main line when it is asked.
		void RenderWindowBar();

		void RenderSidebar();
		void RebuildSidebarWhenOutOfDate();

		// What BuildSidebarRows is given for `inputs`; `mergedByContent` is held by pointer and must outlive the input.
		SidebarInput SidebarInputFor(const SidebarInputs& inputs,
									 const std::function<bool(std::size_t)>& mergedByContent) const;

		// The window bar's HEAD chip clicked: HEAD's history is shown, and while HEAD is on a branch the sidebar scrolls
		// once to that branch's row, clearing a search that hides it and opening the section or group holding it.
		void AnswerHeadChip();

		// A click on Fetch, Pull, Push, or on the one running.
		void AnswerNetworkClick(NetworkClick click);

		// The remotes Push can go to, when it asks which: "Push <branch> to…" and one item each, in the order the remotes are
		// listed, which is by name.
		void RenderPushRemoteMenu();

		// The full name of the branch the sidebar keeps listed: the selected one, or the checked-out one for HEAD's
		// history; empty for a stash's.
		std::string SidebarShownRef() const;

		// What a row stands for in the snapshots its rows were built from; null for another kind of row. A base row stands
		// for its local copy too: the branch it opens and the branches its menu acts on are given by RefOpenedBy and
		// RowMenuRefsOf.
		const Core::Branch* SidebarBranchAt(std::optional<std::size_t> ref) const;
		const Core::StashEntry* SidebarStashOf(const SidebarRow& row) const;
		const Core::Tag* SidebarTagOf(const SidebarRow& row) const;

		std::optional<std::size_t> SelectedTagIndex() const;

		SidebarRowText SidebarTextOf(const SidebarRow& row) const;
		SidebarRowText SidebarWorktreeTextOf(const SidebarRow& row) const; // marks the worktree's row as in view
		SidebarSelection SidebarSelectionNow() const;
		void AnswerSidebarClick(const SidebarRow& row);
		void AnswerSidebarLink(SidebarLink link, const std::string& remote);

		// Fills the menu the sidebar opened for a branch, base or stash row.
		void DrawSidebarRowMenu(const SidebarRow& row);

		// The preview of the history's Local changes row: the Changes view's file list beside the diff of the file picked.
		// Another worktree's changes row shows that worktree's view and diffs.
		void RenderChanges(const GitPalette& palette);
		void RenderWorktreeChanges(const std::filesystem::path& worktree, const GitPalette& palette);
		void RenderChangesDiff(const GitPalette& palette, DiffBody& body, bool picked, GitModel& model);

		// The worktree whose changes row the history shows is followed while the window is on screen; off screen, with no
		// folder or no such row, none is, so nothing stays watched or held.
		void FollowShownWorktree(bool panelOnScreen);

		// The worktree changes' model reads `worktree`: until it does, after a switch that waits for a write, its numbers
		// and details are another worktree's.
		bool WorktreeChangesInHand(const std::filesystem::path& worktree) const;

		// What the worktree's Changes view offers (spec §6.1–6.3): no Discard nor Stash, a commit button naming the target.
		ChangesViewOptions WorktreeViewOptions(const std::filesystem::path& worktree) const;

		void RenderHistory(const GitPalette& palette);
		void RenderHistoryHeader();

		std::string HistoryTitle() const;

		// Which history is shown and how it stands against the read on screen, asked of what is selected and the models
		// now.
		ShownHistory Shown() const;

		// What the shown reference is compared with and how far from it; asked of the lines model on every frame the
		// header is drawn, which answers from its cache after the first.
		std::optional<Core::LineDistance> ShownDistance(std::string_view shownRef);

		// The header counts a distance that is still being measured.
		bool MeasuresShownDistance(std::string_view shownRef);

		// Whether the view's read is the one the models follow; it is not in the one frame a new read lands.
		bool ViewFollowsModelsRead() const;

		// What the check of the shown branch's contents found, asking it first; empty for a reference that is no
		// candidate for one (a pick, an old copy, a base, a branch merged by history).
		std::optional<ContentMark> ShownContentMark();

		// Whether the header and the first rows wait for that check: it is pending and the model can answer it.
		bool ShownContentWaits();

		// For each base that does not hold `commit` by history: whether it holds a copy of it, asked of the content model
		// (the base's tip is the line searched), and the landing of the shown branch's squash when the commit is one of that
		// branch's own and the base its line.
		std::vector<BaseEquivalence> EquivalenceOf(const LineRead& read, const Core::Oid& commit,
												   const std::vector<bool>& holding);

		// What the shown branch's check says of `commit` on the base it is compared with; the copy its verdict answers
		// for, when the verdict names a squash (TwinFromShownSquash).
		std::optional<TwinMark> NoteShownSquash(BaseEquivalence& equivalence, const Core::Oid& commit, std::size_t baseRef);

		// The source sentence of the commit the details show, kept until another is selected.
		KeptSourceSentence m_KeptSource;

		void FollowSelectedRef();

		// `tags` is null while the tags are not listed.
		void FollowHistoryRef(const std::vector<Core::Branch>& branches, const std::vector<Core::Tag>* tags, bool renameLanded);
		void FollowSelectedStash();

		// A selected worktree that has left the list, lost its folder or moved onto a branch shows what SelectWorktree
		// shows for it now; one whose HEAD moved has its history asked again from the new commit.
		void FollowSelectedWorktree();

		// Opens the menu on a right-click of the last item.
		void RenderRefMenu(const Core::Branch& branch);

		// Fills a menu already open. The lines items are for `linesFor`, the full name of the branch itself or of the
		// base whose row the branch is the local copy in; Compare with… selects `comparedFor`, the branch such a row opens.
		void RenderRefMenuItems(const Core::Branch& branch, const std::string& linesFor, const std::string& comparedFor);

		void RenderReviewItem(const Core::Branch& branch);
		void RenderLineItems(const std::string& linesFor, const std::string& comparedFor);
		void RenderWorktreeItem(const Core::Branch& branch);
		void DrawReviewMenuItems(const std::vector<ReviewMenuItem>& items);

		// Fills a menu already open.
		void RenderStashMenuItems(const Core::StashEntry& stash);

		void SelectStash(const Core::Oid& stash);
		void DeselectStash();

		void BeginCheckout(const Core::CheckoutTarget& target);

		// Asks for the checkout and turns the history to the branch checked out, whatever it showed: after checking out a
		// remote branch the history stayed on the branch selected before it.
		void CheckOut(CheckoutRequest request);
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

		// Asks what the page's open merges brought, keeps their pages however many there are, and forgets a failed read of
		// a merge closed since the last frame.
		void AskBroughtByOpenMerges(const Core::Ref<const Core::LogPage>& page, const HistoryMarks& brought);

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

		// The path box names a file, so the history is that file's.
		bool IsFileHistory() const;

		// The changes row the history starts with: the open folder's Local changes, another worktree's "Changes in
		// <folder>", or none.
		void MarkChangesRow(HistoryMarks& marks) const;

		// The page on screen is the history of the reference or stash selected now.
		bool PageAnswersSelection(const Core::LogRequest& shown) const;

		// The list shows the page's rows rather than a wait; the header's banner comes with them. A folded line's
		// rows wait for the fold row's count the first time the history shows them (asked of the lines model here).
		bool ShowsCommitRows();
		HistoryRows HistoryRowsNow();

		// The page asked with the line now shown is in hand with the counts its first rows are drawn with.
		bool FirstRowsReady(const Core::LogRequest& shown);

		// The last page asked of the model was asked with the line now shown, and it has landed.
		bool RowsOfLineShown() const;

		// The header's distance or the folded line's count is still measured; asking them if not asked yet.
		bool ShownLineCountsWait();

		HistoryMarks HistoryMarksFor(const Core::Ref<const Core::LogPage>& page);

		// The marked commits that landed with the page, as a set built once per page of them.
		const std::unordered_set<Core::Oid>& MarkedCommitsOnScreen();

		// "Show them" and "Fold them": the shown branch's line's newer commits beside it, or folded into one row again.
		void ToggleNewerShown();

		void AnswerListClicks();

		// Once the first page of a reference just opened is in, selects the row it starts on and scrolls there.
		void SelectOpenedStart();

		// The shown reference's line as the last log asked of the model was asked with it, and the log generation of that
		// ask: the page that answers it carries the marks of that line.
		std::optional<HistoryLine> m_LineAsked;
		std::uint64_t m_LineAskedGeneration = 0;

		// The history waits for the line read, so its log is not asked yet.
		bool m_LogHeld = false;

		// A reference just opened: its first page selects the row it starts on.
		bool m_OpeningRef = false;

		struct SelectedHistory
		{
			std::optional<std::string> Ref;
			std::optional<Core::Oid> Stash;
			std::optional<std::filesystem::path> Worktree;

			// The branch the rows were shown for, which for HEAD's history moves with a checkout.
			std::string Shown;

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
		NetworkModel& m_Network;
		WorktreesModel& m_Worktrees;
		ContentModel& m_Content;
		ChangesView& m_Changes;
		WorktreeChanges& m_WorktreeChanges;
		ChangesView& m_WorktreeView;
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
			bool HeadFailed = false;
			std::optional<std::string> CheckingOut;
			std::filesystem::path Folder;

			friend bool operator==(const WindowBarSource&, const WindowBarSource&) = default;
		};

		// What the network buttons read besides the bar's source. The age is whole minutes, so the text is described again
		// only when it can change. A change here rebuilds the buttons alone, not the bar's text.
		struct NetworkSource
		{
			NetworkState State;
			std::optional<std::chrono::sys_seconds> LastFetch;
			bool GitFound = false;
			bool FolderTrusted = false;
			std::int64_t NowMinutes = 0;

			friend bool operator==(const NetworkSource&, const NetworkSource&) = default;
		};

		std::optional<NetworkSource> m_NetworkSource;

		// "Push main to…", fixed when the menu opens.
		std::string m_PushMenuTitle;

		std::optional<WindowBarSource> m_WindowBarSource;
		WindowBarText m_WindowBarText;
		NetworkButtons m_NetworkButtons;

		// The last complete bar of this folder, kept while a refresh is incomplete; empty while the first read runs.
		std::optional<WindowBarText> m_WindowBarSettled;

		// "Not now" on the main-line question: until the folder changes.
		bool m_MainLineDismissed = false;

		// The read the sidebar and the history both draw, followed as each frame starts.
		HeldLines m_OnScreen;

		SidebarView m_Sidebar;

		// WARNING: The inputs are held, not only compared: the rows' indices point into the read and the stashes held
		// here, and a snapshot freed and another made at its address must never pass for the one the rows were built
		// from. Without a read the rows are the stashes alone.
		std::optional<SidebarInputs> m_SidebarBuiltFrom;
		std::vector<SidebarRow> m_SidebarRows;
		SearchedAuthors m_SearchedAuthors;

		// The remote branches the rows hide as merged by content, found again only at the moments spec §6 rule 3 names.
		MergedByContentSnapshot m_HiddenByContent;

		// The Worktrees section's order, built again only at the moments its spec names (§4.2).
		WorktreeMoments m_WorktreeMoments;
		WorktreeOrder m_WorktreeOrder;

		// The frame the sidebar was last drawn on: drawn on any later one than the next, the window was opened again.
		int m_SidebarDrawnOnFrame = -1;
		std::uint64_t m_SidebarOpenings = 0;

		// The header's vs label and banner as last drawn with their distance known, kept while it is measured again.
		std::optional<DrawnHeaderWords> m_DrawnHeaderWords;

		// The banner the header found this frame, which the list draws as a row.
		std::optional<std::string> m_HistoryBanner;
		std::optional<std::string> m_HistoryBannerAction;

		// What the shown reference is compared with: opened by the header's vs button and the sidebar's bases link.
		VsPickerView m_VsPicker;
		std::string m_MostCommonLine;

		std::map<std::string, bool> m_OpenByUser;
		std::set<std::string> m_ShowingMerged;
		std::uint64_t m_SidebarChoicesVersion = 0;       // sections and groups opened or closed
		std::uint64_t m_SidebarMergedChoicesVersion = 0; // remotes' merged branches shown or hidden

		// The full name of the branch, or the id of the stash, the open row menu was opened for.
		std::string m_SidebarMenuFor;

		// Asked for only while it is drawn.
		DiffBody m_ChangesDiff;
		bool m_ChangesPicked = false;
		bool m_ChangesDrawn = false;

		// The same for another worktree's changes, and the worktree its picked file is in.
		DiffBody m_WorktreeChangesDiff;
		bool m_WorktreeChangesPicked = false;
		bool m_WorktreeChangesDrawn = false;
		std::optional<std::filesystem::path> m_WorktreeDiffFor;

		// The HEADs the selected detached worktree's history was walked from since it was selected: a page from an earlier
		// one keeps its rows on screen while the page from the new HEAD is read.
		std::unordered_set<Core::Oid> m_WorktreeHeadsWalked;

		std::optional<std::string> m_SelectedRef;

		std::optional<Core::Oid> m_SelectedStash;

		// Set only for a worktree without a branch: a worktree on a branch selects that branch's ref.
		std::optional<std::filesystem::path> m_SelectedWorktree;

		// The branch a click on its worktree's row selected, so that row, not the branch's in Yours, is drawn selected;
		// let go of by any other selection.
		std::optional<std::string> m_RefOpenedFromWorktreeRow;

		Core::Ref<const std::vector<Core::Branch>> m_LastSeenBranches;
		Core::Ref<const std::vector<Core::Tag>> m_LastSeenTags;
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

		// The page's commits by id, built once per page, so the open merges are found in it without a pass over the page.
		Core::Ref<const Core::LogPage> m_CommitIndexOf;
		std::unordered_map<Core::Oid, std::size_t> m_CommitIndex;

		// The merges open in the page last frame, whose brought commits the model keeps, and those asked with them.
		std::unordered_set<Core::Oid> m_OpenMerges;
		std::unordered_set<Core::Oid> m_MergesAsked;

		// A commit asked for from outside the panel, waiting for a page that holds it.
		std::optional<CommitReveal> m_PendingReveal;

		// The file a reveal selected, kept until the commit's diff says whether it lists the file.
		std::optional<FileSelection> m_RevealedFile;

		FileTextsBinding m_Texts;

		std::optional<Core::CheckoutTarget> m_PendingCheckout;

		HeldCheckout m_HeldCheckout;
		std::optional<Core::CheckoutTarget> m_AskedCheckout;

		BranchDeletion m_Deletion;

		// The listings the pills on history rows and in the details were built from: a fetch can change the tags alone.
		struct CommitRefsSource
		{
			Core::Ref<const std::vector<Core::Branch>> Branches;
			Core::Ref<const std::vector<Core::Tag>> Tags;

			friend bool operator==(const CommitRefsSource&, const CommitRefsSource&) = default;
		};

		CommitRefsSource m_CommitRefsOf;
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
