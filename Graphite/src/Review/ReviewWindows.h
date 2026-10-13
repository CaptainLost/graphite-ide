#pragma once

#include "Core/Base/Base.h"

#include "Git/Diff/DiffView.h"
#include "Git/Diff/FileTexts.h"
#include "Persistence/PersistedState.h"
#include "Review/ReviewFiles.h"
#include "Review/ReviewModel.h"
#include "Review/ViewedMarks.h"
#include "Theme/GitPalette.h"
#include "Theme/Palettes.h"

#include "Core/Git/Ref.h"
#include "Core/ImGui/TabCloseRequests.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct ReviewTarget
	{
		std::string Branch;

		// Absent until the reader picks one.
		std::optional<std::string> Base;
	};

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
		ViewedMarks& Marks;

		// Null until the first snapshot; the base field then offers nothing to pick.
		Core::Ref<const std::vector<Core::Branch>> Branches;

		const GitPalette& Palette;
		const SyntaxPalette& Syntax;
		ImU32 Selection = 0;
		bool WordWrap = false;
		unsigned int CentreNode = 0;
		Core::TabCloseRequests& CloseRequests;
	};

	// Reviews open as tabs in the document area, one per reviewed branch. Rendered by GraphiteLayer
	// beside the diff tabs, so a review keeps drawing whichever panel has focus.
	class ReviewWindows
	{
	public:
		// A branch already under review has its tab raised, keeping the base it has.
		void Open(const ReviewTarget& target);

		bool IsOpen(const std::string& branchFullName) const;
		std::size_t GetCount() const { return m_Tabs.size(); }

		// Every tab closed, and what the model held for each forgotten.
		void CloseAll(ReviewModel& model);

		std::vector<ReviewTabState> CaptureState() const;

		// Reopens what CaptureState returned. None is brought forward, and each goes back to the
		// place imgui.ini remembers for it.
		void RestoreState(const std::vector<ReviewTabState>& reviews);

		// A rename sent from the Git window, followed once a branch snapshot shows it took effect.
		void NoteRename(std::string from, std::string to);

		// Called with every new branch snapshot. Follows the renames that took effect, retargeting
		// their tabs, and closes a restored tab whose branch the first snapshot lacks. Answers the
		// renames it followed.
		[[nodiscard]] std::vector<BranchRename> FollowBranches(const std::vector<Core::Branch>& branches,
															   ReviewModel& model);

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
		// The file list as last built, rebuilt only when a diff or a fold changed: sorting every
		// file of a large branch by path on every frame would cost the frame.
		struct RowsCache
		{
			Core::Ref<const Core::Diff> Committed;
			Core::Ref<const Core::Diff> Uncommitted;
			std::array<bool, kReviewGroupCount> Collapsed{};
			std::vector<ReviewListRow> Rows;

			// Parallel to Rows; read only for a file.
			std::vector<LineCounts> Counts;
			bool Built = false;

			ViewedCount Viewed;
			std::uint64_t ViewedForVersion = 0;
			bool ViewedCounted = false;
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
			ReviewTarget Target;
			bool IncludeUncommitted = false;
			std::optional<ReviewSelection> Selected;

			// What each group listed when the rows were last built, so a selected file that
			// disappears gives way to the one that took its place.
			std::array<std::vector<std::filesystem::path>, kReviewGroupCount> LastPaths;
			std::array<bool, kReviewGroupCount> Collapsed{};
			RowsCache Rows;

			DiffView View;
			Core::Ref<const FileTexts> Texts;
			FileTokens Tokens;

			bool ScrollToSelected = false;
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

		using OpenRelativeFile = std::function<void(const std::filesystem::path&)>;

		static std::optional<ReviewKey> KeyOf(const Tab& tab);
		static void ForgetTab(const Tab& tab, ReviewModel& model);
		static ReviewGroups GroupsOf(const Tab& tab, const LoadedReview& review);
		static void ChangeBase(Tab& tab, ReviewModel& model, const std::string& base);
		static void SetIncludeUncommitted(Tab& tab, ReviewModel& model, bool include);
		static void ToggleViewed(const Tab& tab, ViewedMarks& marks, ReviewGroup group, const Core::FileDiff& file);
		static void RefreshRows(Tab& tab, const LoadedReview& review);
		static ViewedCount ViewedCountOf(Tab& tab, const ViewedMarks& marks, const ReviewGroups& groups);
		static void UpdateTexts(Tab& tab, ReviewModel& model, const LoadedReview& review, const Core::FileDiff& file);

		// Joined onto the worktree holding the branch, whose files are the branch's version; null
		// when no worktree holds it.
		OpenRelativeFile OpenFileIn(const ReviewRange& range) const;

		// In ReviewTabDrawing.cpp.
		void RenderTab(Tab& tab, const ReviewRenderInput& input);
		void RenderHeader(Tab& tab, const ReviewRenderInput& input, const ReviewRange* range,
						  const std::optional<ViewedCount>& viewed);
		void RenderBody(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review);
		void RenderFileList(Tab& tab, const ReviewRenderInput& input, const ReviewGroups& groups,
							const OpenRelativeFile& openFile);
		void RenderFileRow(Tab& tab, const ReviewRenderInput& input, const ReviewGroups& groups,
						   const ReviewListRow& row, const LineCounts& counts, bool isSelected,
						   const OpenRelativeFile& openFile);
		void HandleListKeys(Tab& tab, ViewedMarks& marks, const ReviewGroups& groups);
		void RenderSelectedDiff(Tab& tab, const ReviewRenderInput& input, const LoadedReview& review,
								const ReviewGroups& groups, const OpenRelativeFile& openFile);

		std::vector<Tab> m_Tabs;
		std::vector<BranchRename> m_PendingRenames;
	};
}
