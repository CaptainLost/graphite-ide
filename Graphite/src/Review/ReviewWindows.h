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
#include "UI/Dialogs/ModalGate.h"
#include "UI/Dialogs/ModalQuestion.h"
#include "UI/TabWindow.h"

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

	struct BranchRename
	{
		std::string From;
		std::string To;
	};

	// WARNING: This reaches imgui.ini, so it never changes once shipped.
	std::string ReviewWindowIdentity(const std::string& branchFullName);

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

	enum class RowMarks
	{
		Shown,
		Hidden
	};

	class ReviewWindows
	{
	public:
		explicit ReviewWindows(ModalGate& modals);

		// WARNING: Start the branch's session first: a tab whose branch has no session closes on the
		// next FollowSessions.
		void Open(const std::string& branch);

		bool IsOpen(const std::string& branchFullName) const;
		std::size_t GetCount() const { return m_Tabs.size(); }

		void CloseAll(ReviewModel& model);

		std::vector<ReviewTabState> CaptureState() const;

		void RestoreState(const std::vector<ReviewTabState>& reviews, const ReviewSessions& sessions);

		void NoteRename(std::string from, std::string to);

		[[nodiscard]] std::vector<BranchRename> FollowBranches(const std::vector<Core::Branch>& branches,
															   ReviewModel& model);

		void FollowSessions(const ReviewSessions& sessions, ReviewModel& model);

		void AskToEnd(const std::string& branch);

		bool IsAskingToEnd() const;

		void Render(const ReviewRenderInput& input);

		std::function<void(const std::filesystem::path&)> OpenFile;

		std::function<void(std::string_view)> OnCopied;

		std::function<void(bool)> OnSetWordWrap;

		std::function<void(std::string_view what, const std::string& text)> CopyText;

	private:
		static constexpr std::size_t kFilterCapacity = 256;

		enum class ReviewPage
		{
			Files,
			Commits
		};

		// WARNING: Rebuilt only when what it was built from changes: sorting every file of a large branch
		// into a tree on every frame would cost the frame.
		struct RowsCache
		{
			Core::Ref<const Core::Diff> Committed;
			Core::Ref<const Core::Diff> Uncommitted;
			std::array<bool, kReviewGroupCount> Collapsed{};
			std::string Filter;
			ListShape Shape = ListShape::Tree;
			bool HideViewed = false;
			std::uint64_t FoldsVersion = 0;

			std::uint64_t SessionsVersion = 0;
			bool Built = false;

			std::vector<ReviewTreeRow> Rows;

			std::array<std::vector<LineCounts>, kReviewGroupCount> FileCounts;
			std::array<LineCounts, kReviewGroupCount> GroupCounts{};

			std::string Summary;

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

			std::optional<ReviewKey> Key;
			std::optional<ReviewSelection> Selected;

			std::array<std::vector<std::filesystem::path>, kReviewGroupCount> LastPaths;
			std::array<bool, kReviewGroupCount> Collapsed{};
			std::array<char, kFilterCapacity> Filter{};

			std::set<MarkKey> Folded;
			std::uint64_t FoldsVersion = 0;
			RowsCache Rows;

			ReviewPage Page = ReviewPage::Files;
			std::optional<Core::Oid> SelectedCommit;
			std::optional<ReviewSelection> SelectedCommitFile;

			std::set<MarkKey> FoldedInCommit;
			std::uint64_t FoldsInCommitVersion = 0;
			RowsCache CommitRows;
			std::vector<std::filesystem::path> LastCommitPaths;

			float CommitListHeight = 0.0f;

			DiffView View;
			Core::Ref<const FileTexts> Texts;
			FileTokens Tokens;

			bool ScrollToSelected = false;
			bool ScrollToCommit = false;
			bool ScrollToCommitFile = false;
			TabWindowState Window{.Focus = true};
			bool CloseRequested = false;

			std::optional<LoadedReview> LastLoaded;

			std::optional<std::filesystem::path> LastWorktree;

			bool AwaitingBranches = false;
		};

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

		static void FollowNewDiffs(Tab& tab, const LoadedReview& review);
		static void UpdateTexts(Tab& tab, ReviewModel& model, const LoadedReview& review, const Core::FileDiff& file);

		static void HoldTexts(Tab& tab, const Core::Ref<const FileTexts>& texts);

		static void PickCommit(Tab& tab, const std::optional<Core::Oid>& commit);

		static void FollowCommits(Tab& tab, const ReviewRange& range);

		static void RefreshCommitRows(Tab& tab, const SessionSettings& settings, const Core::CommitSummary& commit,
									  const Core::Ref<const Core::Diff>& commitDiff);

		OpenRelativeFile OpenFileIn(const ReviewRange& range) const;

		void RenderTab(Tab& tab, const ReviewRenderInput& input);

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

		void RenderFileDiff(Tab& tab, const ReviewRenderInput& input, const Core::FileDiff& file,
							const OpenRelativeFile& openFile);

		void RenderEndDialog(const ReviewRenderInput& input);

		std::vector<Tab> m_Tabs;
		ModalQuestion<std::string> m_EndQuestion;
		std::vector<BranchRename> m_PendingRenames;
	};
}
