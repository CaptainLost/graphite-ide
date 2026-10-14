#pragma once

#include "Core/Base/Base.h"

#include "Git/Diff/DiffView.h"
#include "Git/Diff/FileTexts.h"
#include "Persistence/PersistedState.h"
#include "Review/ReviewFiles.h"
#include "Review/ReviewModel.h"
#include "Review/ReviewSessions.h"
#include "Review/ReviewTree.h"
#include "Theme/GitPalette.h"
#include "Theme/Palettes.h"
#include "UI/ModalGate.h"
#include "UI/ModalQuestion.h"

#include "Core/Git/Ref.h"
#include "Core/ImGui/TabCloseRequests.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// A rename of a branch that took effect, for whatever keeps data by branch to follow too.
	struct BranchRename
	{
		std::string From;
		std::string To;
	};

	// What ImGui keys a review tab by. It reaches imgui.ini, so it never changes once shipped.
	std::string ReviewWindowIdentity(const std::string& branchFullName);

	// The label and the identity joined the way ImGui wants them: renaming the label moves no window.
	std::string ReviewWindowName(const std::string& branchFullName);

	struct ReviewRenderInput
	{
		ReviewModel& Model;
		ReviewSessions& Sessions;
		ImVec4 Danger;
		const GitPalette& Palette;
		const SyntaxPalette& Syntax;
		ImU32 Selection = 0;
		bool WordWrap = false;
		unsigned int CentreNode = 0;
		Core::TabCloseRequests& CloseRequests;
	};

	// Whether a review's file list draws the viewed marks: the Files page's does, a commit's has none.
	enum class RowMarks
	{
		Shown,
		Hidden
	};

	// Reviews open as tabs in the document area, one per reviewed branch. Rendered by GraphiteLayer
	// beside the diff tabs, so a review keeps drawing whichever panel has focus.
	class ReviewWindows
	{
	public:
		explicit ReviewWindows(ModalGate& modals);

		// Opens the branch's tab, or raises it when it is open. Start the branch's session first: a tab
		// whose branch has no session closes on the next FollowSessions.
		void Open(const std::string& branch);

		bool IsOpen(const std::string& branchFullName) const;
		std::size_t GetCount() const { return m_Tabs.size(); }

		// Every tab closed, and what the model held for each forgotten.
		void CloseAll(ReviewModel& model);

		std::vector<ReviewTabState> CaptureState() const;

		// Reopens what CaptureState returned, leaving out a branch whose session is gone. A selection
		// among uncommitted files is dropped when the session leaves them out. None is brought
		// forward, and each goes back to the place imgui.ini remembers for it.
		void RestoreState(const std::vector<ReviewTabState>& reviews, const ReviewSessions& sessions);

		// A rename sent from the Git window, followed once a branch snapshot shows it took effect.
		void NoteRename(std::string from, std::string to);

		// Called with every new branch snapshot. Follows the renames that took effect to the renamed
		// branch's tab, and closes a restored tab whose branch the first snapshot lacks. Answers the
		// renames it followed, for the sessions.
		[[nodiscard]] std::vector<BranchRename> FollowBranches(const std::vector<Core::Branch>& branches,
															   ReviewModel& model);

		// Called first thing in every Render. A tab whose session is gone closes; a tab whose session
		// now names its base differently forgets the range it held and keeps its selection.
		void FollowSessions(const ReviewSessions& sessions, ReviewModel& model);

		// Asks before ending the branch's review. A yes forgets what the model held for it, ends its
		// session - marks and all - and closes its tab.
		void AskToEnd(const std::string& branch);

		bool IsAskingToEnd() const;

		void Render(const ReviewRenderInput& input);

		// Opens a file in the editor, given its absolute path.
		std::function<void(const std::filesystem::path&)> OpenFile;

		// Confirms a copy out of a tab's diff; see DiffView::OnCopied.
		std::function<void(std::string_view)> OnCopied;

		// Sets the word wrap every diff shares; see DiffView::OnSetWordWrap.
		std::function<void(bool)> OnSetWordWrap;

		// Puts text on the clipboard and confirms it, naming what was copied.
		std::function<void(std::string_view what, const std::string& text)> CopyText;

	private:
		static constexpr std::size_t kFilterCapacity = 256;

		// Which of the left column's pages is open, for the run of the program only.
		enum class ReviewPage
		{
			Files,
			Commits
		};

		// A file list as last built and what it was built from, rebuilt only when one of those
		// changes: sorting every file of a large branch into a tree on every frame would cost the frame.
		// The Files page's list fills all of it; a picked commit's list has one group, no headings and
		// no marks, and leaves what those need alone.
		struct RowsCache
		{
			Core::Ref<const Core::Diff> Committed;
			Core::Ref<const Core::Diff> Uncommitted;
			std::array<bool, kReviewGroupCount> Collapsed{};
			std::string Filter;
			ListShape Shape = ListShape::Tree;
			bool HideViewed = false;
			std::uint64_t FoldsVersion = 0;

			// The viewed counts of the folders and the headings follow the marks.
			std::uint64_t SessionsVersion = 0;
			bool Built = false;

			std::vector<ReviewTreeRow> Rows;

			// Worked out only when a diff changes, so typing in the filter does not count every line
			// again. Each group's files by their index into its diff.
			std::array<std::vector<LineCounts>, kReviewGroupCount> FileCounts;
			std::array<LineCounts, kReviewGroupCount> GroupCounts{};

			// The header's "2 files +2 -1", or the picked commit's "77d5037 · Ada · 2 files +2 -1".
			std::string Summary;

			// Summed from the heading rows, for the header's "3/7 viewed".
			ViewedCount Viewed;
		};

		struct LoadedReview
		{
			ReviewKey Key;
			Core::Ref<const ReviewRange> Range;
			Core::Ref<const Core::Diff> Committed;
			Core::Ref<const Core::Diff> Uncommitted;
		};

		struct Tab
		{
			std::string Branch;

			// The key this tab last asked the model about.
			std::optional<ReviewKey> Key;
			std::optional<ReviewSelection> Selected;

			// What each group listed when the rows were last built, so a selected file that
			// disappears gives way to the one that took its place.
			std::array<std::vector<std::filesystem::path>, kReviewGroupCount> LastPaths;
			std::array<bool, kReviewGroupCount> Collapsed{};
			std::array<char, kFilterCapacity> Filter{};

			// Folded folders by group and path, for the run of the program only.
			std::set<MarkKey> Folded;
			std::uint64_t FoldsVersion = 0;
			RowsCache Rows;

			// The open page, the commit picked on the Commits page and the file picked in that commit, for
			// the run of the program only.
			ReviewPage Page = ReviewPage::Files;
			std::optional<Core::Oid> SelectedCommit;
			std::optional<ReviewSelection> SelectedCommitFile;

			// The picked commit's list, kept as the Files page's is: its folds, its rows, and what it listed
			// when they were last built.
			std::set<MarkKey> FoldedInCommit;
			std::uint64_t FoldsInCommitVersion = 0;
			RowsCache CommitRows;
			std::vector<std::filesystem::path> LastCommitPaths;

			// The commit list's height above the picked commit, as DrawSplitResults keeps it.
			float CommitListHeight = 0.0f;

			DiffView View;
			Core::Ref<const FileTexts> Texts;
			FileTokens Tokens;

			bool ScrollToSelected = false;
			bool ScrollToCommit = false;
			bool ScrollToCommitFile = false;
			bool Fresh = true;
			bool Focus = true;
			bool Restored = false;
			bool CloseRequested = false;

			// The last review whose committed diff had arrived, drawn while the next one loads so a
			// commit to the branch does not blank the tab.
			std::optional<LoadedReview> LastLoaded;

			// The worktree the range last named, for saying it is gone once it no longer does.
			std::optional<std::filesystem::path> LastWorktree;

			// A restored tab before the first branch snapshot, which decides whether it stays.
			bool AwaitingBranches = false;
		};

		// One of a tab's two file lists, as its rows are drawn and stepped through: the Files page's, with
		// the viewed marks, or the picked commit's, without. It refers to the tab's own state.
		struct FileList
		{
			RowsCache& Rows;
			ReviewGroups Groups;
			std::optional<ReviewSelection>& Selected;
			std::set<MarkKey>& Folded;
			std::uint64_t& FoldsVersion;
			bool& ScrollToSelected;
			RowMarks Marks = RowMarks::Shown;
			const char* ChildId = nullptr;
		};

		using OpenRelativeFile = std::function<void(const std::filesystem::path&)>;

		static void ForgetTab(const Tab& tab, ReviewModel& model);
		static ReviewGroups GroupsOf(const Tab& tab, const LoadedReview& review);
		static FileList FilesPageList(Tab& tab, const ReviewGroups& groups);
		static FileList CommitFileList(Tab& tab, const Core::Diff& commitDiff);
		static void SetIncludeUncommitted(Tab& tab, ReviewSessions& sessions, ReviewModel& model, bool include);
		static void ToggleViewed(const Tab& tab, ReviewSessions& sessions, ReviewGroup group, const Core::FileDiff& file);
		static void RefreshRows(Tab& tab, const ReviewSessions& sessions, const SessionSettings& settings,
								const LoadedReview& review);

		// Only for new diffs: a filter, Hide viewed or a fold keeps the selection even when it hides
		// the selected file, whose diff stays on the right.
		static void FollowNewDiffs(Tab& tab, const LoadedReview& review);
		static void UpdateTexts(Tab& tab, ReviewModel& model, const LoadedReview& review, const Core::FileDiff& file);

		// Copies the tokens only when a different texts object arrives.
		static void HoldTexts(Tab& tab, const Core::Ref<const FileTexts>& texts);

		// A different commit lets go of the file picked in the one before.
		static void PickCommit(Tab& tab, const std::optional<Core::Oid>& commit);

		// Called on every frame: a picked commit the range no longer lists - rebased away - is let go.
		static void FollowCommits(Tab& tab, const ReviewRange& range);

		// The picked commit's rows, built as RefreshRows builds the Files page's. A new diff picks the
		// file that took the place of the one picked, or the first file listed, and scrolls it into view.
		static void RefreshCommitRows(Tab& tab, const SessionSettings& settings, const Core::CommitSummary& commit,
									  const Core::Ref<const Core::Diff>& commitDiff);

		// Joined onto the worktree holding the branch, whose files are the branch's version; null
		// when no worktree holds it.
		OpenRelativeFile OpenFileIn(const ReviewRange& range) const;

		// In ReviewTabDrawing.cpp.
		void RenderTab(Tab& tab, const ReviewRenderInput& input);

		// Without rows while the review has no diff to list: no change summary and no viewed count yet.
		void RenderHeader(Tab& tab, const ReviewRenderInput& input, const ReviewRange* range, const RowsCache* rows);
		void RenderBody(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review);
		void RenderPages(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review,
						 const ReviewGroups& groups, const OpenRelativeFile& openFile);
		void RenderFileList(Tab& tab, const ReviewRenderInput& input, const FileList& list,
							const OpenRelativeFile& openFile);
		void RenderTreeRow(Tab& tab, const ReviewRenderInput& input, const FileList& list, const ReviewTreeRow& row,
						   const OpenRelativeFile& openFile);
		void RenderFileRow(Tab& tab, const ReviewRenderInput& input, const FileList& list, const ReviewTreeRow& row,
						   const OpenRelativeFile& openFile);
		void HandleListKeys(Tab& tab, ReviewSessions& sessions, const FileList& list);
		void RenderCommitsPage(Tab& tab, const ReviewRenderInput& input, const ReviewRange& range,
							   const OpenRelativeFile& openFile);
		void RenderCommitList(Tab& tab, const ReviewRange& range);
		void RenderCommitRows(Tab& tab, const ReviewRange& range);
		void HandleCommitKeys(Tab& tab, const ReviewRange& range);
		void RenderPickedCommit(Tab& tab, const ReviewRenderInput& input, const ReviewRange& range,
								const OpenRelativeFile& openFile);

		// The right column: a header row with the diff's switches, then the diff of the page's picked file.
		void RenderDiffColumn(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review,
							  const ReviewGroups& groups, const OpenRelativeFile& openFile);
		void RenderSelectedDiff(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review,
								const ReviewGroups& groups, const OpenRelativeFile& openFile);
		void RenderSinceViewed(Tab& tab, const ReviewRenderInput& input, const SinceViewedRequest& request,
							   const OpenRelativeFile& openFile);
		void RenderCommitFileDiff(Tab& tab, const ReviewRenderInput& input, const ReviewRange& range,
								  const OpenRelativeFile& openFile);
		void RenderPickedFileDiff(Tab& tab, const ReviewRenderInput& input, const ReviewRange& range,
								  const SessionSettings& settings, const OpenRelativeFile& openFile);

		// Draws the file with the texts HoldTexts last took.
		void RenderFileDiff(Tab& tab, const ReviewRenderInput& input, const Core::FileDiff& file,
							const OpenRelativeFile& openFile);

		void RenderEndDialog(const ReviewRenderInput& input);

		std::vector<Tab> m_Tabs;
		ModalQuestion<std::string> m_EndQuestion;
		std::vector<BranchRename> m_PendingRenames;
	};
}
