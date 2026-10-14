#include "Review/ReviewWindows.h"

#include "Theme/Palettes.h"
#include "UI/DialogLayout.h"
#include "UI/RememberedPlace.h"

#include "Core/Base/Log.h"
#include "Core/ImGui/TabContextMenu.h"

#include <IconsMaterialDesignIcons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <utility>

namespace Graphite {

	namespace {

		constexpr std::string_view kIdentityPrefix = "review:";

		bool ListsBranchNamed(const std::vector<Core::Branch>& branches, const std::string& fullName)
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

		ReviewGroup GroupOfStoredSelection(bool selectedIsUncommitted)
		{
			return selectedIsUncommitted ? ReviewGroup::Uncommitted : ReviewGroup::Commits;
		}

		constexpr const char* kEndQuestionTitle = "###ReviewEnd";

		std::vector<LineCounts> LineCountsOfEachIn(const Core::Diff* groupDiff)
		{
			return groupDiff != nullptr ? LineCountsOfEach(*groupDiff) : std::vector<LineCounts>{};
		}

		ViewedCount ViewedOfHeadings(const std::vector<ReviewTreeRow>& rows)
		{
			ViewedCount count;

			for (const ReviewTreeRow& row : rows)
			{
				if (row.Kind == TreeRowKind::Heading)
				{
					count.Viewed += row.Viewed;
					count.Total += row.Total;
				}
			}

			return count;
		}
	}

	ReviewWindows::ReviewWindows(ModalGate& modals)
		: m_EndQuestion(modals, kEndQuestionTitle)
	{
	}

	std::string ReviewWindowIdentity(const std::string& branchFullName)
	{
		return std::string{kIdentityPrefix} + branchFullName;
	}

	std::string ReviewWindowName(const std::string& branchFullName)
	{
		return std::string{ICON_MDI_SOURCE_PULL} + "  Review: " + ShortBranchName(branchFullName) + "###" +
			   ReviewWindowIdentity(branchFullName);
	}

	void ReviewWindows::ForgetTab(const Tab& tab, ReviewModel& model)
	{
		if (tab.Key.has_value())
		{
			model.Forget(*tab.Key);
		}
	}

	void ReviewWindows::Open(const std::string& branch)
	{
		for (Tab& tab : m_Tabs)
		{
			if (tab.Branch == branch)
			{
				tab.Focus = true;
				return;
			}
		}

		Tab tab;
		tab.Branch = branch;
		m_Tabs.push_back(std::move(tab));
	}

	bool ReviewWindows::IsOpen(const std::string& branchFullName) const
	{
		for (const Tab& tab : m_Tabs)
		{
			if (tab.Branch == branchFullName)
			{
				return true;
			}
		}

		return false;
	}

	void ReviewWindows::CloseAll(ReviewModel& model)
	{
		for (const Tab& tab : m_Tabs)
		{
			ForgetTab(tab, model);
		}

		m_Tabs.clear();
		m_PendingRenames.clear();

		// A yes after the folder changed would end a same-named branch's session in the new folder.
		if (m_EndQuestion.IsAsked())
		{
			m_EndQuestion.Withdraw();
		}
	}

	bool ReviewWindows::IsAskingToEnd() const
	{
		return m_EndQuestion.IsAsked();
	}

	std::vector<ReviewTabState> ReviewWindows::CaptureState() const
	{
		std::vector<ReviewTabState> reviews;
		reviews.reserve(m_Tabs.size());

		for (const Tab& tab : m_Tabs)
		{
			ReviewTabState review{.Branch = tab.Branch};

			if (tab.Selected.has_value())
			{
				review.Selected = tab.Selected->Path;
				review.SelectedIsUncommitted = tab.Selected->Group == ReviewGroup::Uncommitted;
			}

			reviews.push_back(std::move(review));
		}

		return reviews;
	}

	void ReviewWindows::RestoreState(const std::vector<ReviewTabState>& reviews, const ReviewSessions& sessions)
	{
		for (const ReviewTabState& stored : reviews)
		{
			if (IsOpen(stored.Branch))
			{
				continue;
			}

			const ReviewSession* session = sessions.Find(stored.Branch);

			if (session == nullptr)
			{
				APP_INFO("Not reopening the review of '{}': it has no session", stored.Branch);
				continue;
			}

			Tab tab;
			tab.Branch = stored.Branch;

			const bool selectionCanShow = !stored.Selected.empty() &&
										  (session->Settings.IncludeUncommitted || !stored.SelectedIsUncommitted);

			if (selectionCanShow)
			{
				tab.Selected = ReviewSelection{.Group = GroupOfStoredSelection(stored.SelectedIsUncommitted),
											   .Path = stored.Selected};
			}

			tab.Focus = false;
			tab.Restored = true;
			tab.AwaitingBranches = true;
			m_Tabs.push_back(std::move(tab));
		}
	}

	void ReviewWindows::NoteRename(std::string from, std::string to)
	{
		m_PendingRenames.push_back(BranchRename{.From = std::move(from), .To = std::move(to)});
	}

	std::vector<BranchRename> ReviewWindows::FollowBranches(const std::vector<Core::Branch>& branches,
															ReviewModel& model)
	{
		std::vector<BranchRename> followed;
		std::vector<BranchRename> stillPending;

		for (BranchRename& rename : m_PendingRenames)
		{
			if (ListsBranchNamed(branches, rename.From))
			{
				stillPending.push_back(std::move(rename));
				continue;
			}

			// Gone under its old name and absent under its new one: the rename did not take.
			if (!ListsBranchNamed(branches, rename.To))
			{
				continue;
			}

			const bool targetHasItsOwnTab = IsOpen(rename.To);

			for (std::size_t tabIndex = 0; tabIndex < m_Tabs.size();)
			{
				Tab& tab = m_Tabs[tabIndex];

				if (tab.Branch != rename.From)
				{
					++tabIndex;
					continue;
				}

				ForgetTab(tab, model);

				if (targetHasItsOwnTab)
				{
					m_Tabs.erase(m_Tabs.begin() + static_cast<std::ptrdiff_t>(tabIndex));
					continue;
				}

				tab.Key.reset();
				tab.Branch = rename.To;
				tab.LastLoaded.reset();
				tab.LastWorktree.reset();

				// A new identity: ImGui has no place for this window yet, so it is docked again.
				tab.Fresh = true;
				tab.Focus = true;
				++tabIndex;
			}

			followed.push_back(std::move(rename));
		}

		m_PendingRenames = std::move(stillPending);

		for (std::size_t tabIndex = 0; tabIndex < m_Tabs.size();)
		{
			Tab& tab = m_Tabs[tabIndex];

			if (!tab.AwaitingBranches)
			{
				++tabIndex;
				continue;
			}

			tab.AwaitingBranches = false;

			if (ListsBranchNamed(branches, tab.Branch))
			{
				++tabIndex;
				continue;
			}

			APP_INFO("Not reopening the review of '{}': that branch is gone", tab.Branch);
			ForgetTab(tab, model);
			m_Tabs.erase(m_Tabs.begin() + static_cast<std::ptrdiff_t>(tabIndex));
		}

		return followed;
	}

	ReviewGroups ReviewWindows::GroupsOf(const Tab& tab, const LoadedReview& review)
	{
		return ReviewGroups{.Commits = review.Committed.get(),
							.Uncommitted = review.Uncommitted.get(),
							.Collapsed = tab.Collapsed};
	}

	ReviewWindows::FileList ReviewWindows::FilesPageList(Tab& tab, const ReviewGroups& groups)
	{
		return FileList{.Rows = tab.Rows,
						.Groups = groups,
						.Selected = tab.Selected,
						.Folded = tab.Folded,
						.FoldsVersion = tab.FoldsVersion,
						.ScrollToSelected = tab.ScrollToSelected,
						.Marks = RowMarks::Shown,
						.ChildId = "##reviewFiles"};
	}

	ReviewWindows::FileList ReviewWindows::CommitFileList(Tab& tab, const Core::Diff& commitDiff)
	{
		return FileList{.Rows = tab.CommitRows,
						.Groups = ReviewGroups{.Commits = &commitDiff},
						.Selected = tab.SelectedCommitFile,
						.Folded = tab.FoldedInCommit,
						.FoldsVersion = tab.FoldsInCommitVersion,
						.ScrollToSelected = tab.ScrollToCommitFile,
						.Marks = RowMarks::Hidden,
						.ChildId = "##reviewCommitFiles"};
	}

	void ReviewWindows::FollowSessions(const ReviewSessions& sessions, ReviewModel& model)
	{
		for (std::size_t tabIndex = 0; tabIndex < m_Tabs.size();)
		{
			Tab& tab = m_Tabs[tabIndex];
			const ReviewSession* session = sessions.Find(tab.Branch);

			if (session == nullptr)
			{
				ForgetTab(tab, model);
				m_Tabs.erase(m_Tabs.begin() + static_cast<std::ptrdiff_t>(tabIndex));
				continue;
			}

			const ReviewKey key{.Branch = tab.Branch, .Base = session->Base};

			// The selection stays: the base under its new name covers the same range.
			if (tab.Key.has_value() && *tab.Key != key)
			{
				model.Forget(*tab.Key);
				tab.LastLoaded.reset();
			}

			tab.Key = key;
			++tabIndex;
		}
	}

	void ReviewWindows::AskToEnd(const std::string& branch)
	{
		if (!m_EndQuestion.Asked(branch))
		{
			m_EndQuestion.Ask(branch);
		}
	}

	void ReviewWindows::SetIncludeUncommitted(Tab& tab, ReviewSessions& sessions, ReviewModel& model, bool include)
	{
		const ReviewSession* session = sessions.Find(tab.Branch);

		if (session == nullptr)
		{
			return;
		}

		SessionSettings settings = session->Settings;
		settings.IncludeUncommitted = include;
		sessions.SetSettings(tab.Branch, settings);
		tab.Rows.Built = false;

		if (include)
		{
			return;
		}

		if (tab.Key.has_value())
		{
			model.StopUncommitted(*tab.Key);
		}

		const bool selectionWasUncommitted =
			tab.Selected.has_value() && tab.Selected->Group == ReviewGroup::Uncommitted;

		if (selectionWasUncommitted)
		{
			tab.Selected.reset();
		}
	}

	void ReviewWindows::ToggleViewed(const Tab& tab, ReviewSessions& sessions, ReviewGroup group,
									 const Core::FileDiff& file)
	{
		if (sessions.StateOf(tab.Branch, group, file) == ViewedState::Viewed)
		{
			sessions.Unmark(tab.Branch, group, file);
			return;
		}

		sessions.Mark(tab.Branch, group, file);
	}

	void ReviewWindows::RefreshRows(Tab& tab, const ReviewSessions& sessions, const SessionSettings& settings,
									const LoadedReview& review)
	{
		RowsCache& cache = tab.Rows;
		const std::string_view filter{tab.Filter.data()};

		const bool diffsChanged =
			!cache.Built || cache.Committed != review.Committed || cache.Uncommitted != review.Uncommitted;
		const bool sameList = !diffsChanged && cache.Collapsed == tab.Collapsed && cache.Filter == filter &&
							  cache.Shape == settings.Shape && cache.HideViewed == settings.HideViewed &&
							  cache.FoldsVersion == tab.FoldsVersion && cache.SessionsVersion == sessions.GetVersion();

		if (sameList)
		{
			return;
		}

		const std::string& branch = tab.Branch;
		const ReviewTreeInput input{.Groups = GroupsOf(tab, review),
									.StateOf = [&sessions, &branch](ReviewGroup group, const Core::FileDiff& file)
									{ return sessions.StateOf(branch, group, file); },
									.Shape = settings.Shape,
									.Headings = GroupHeadings::Shown,
									.Filter = std::string{filter},
									.HideViewed = settings.HideViewed,
									.FoldedFolders = &tab.Folded};

		cache.Rows = BuildReviewTree(input);
		cache.Viewed = ViewedOfHeadings(cache.Rows);
		cache.Collapsed = tab.Collapsed;
		cache.Filter = input.Filter;
		cache.Shape = settings.Shape;
		cache.HideViewed = settings.HideViewed;
		cache.FoldsVersion = tab.FoldsVersion;
		cache.SessionsVersion = sessions.GetVersion();
		cache.Built = true;

		if (diffsChanged)
		{
			cache.Committed = review.Committed;
			cache.Uncommitted = review.Uncommitted;
			cache.FileCounts[IndexOf(ReviewGroup::Commits)] = LineCountsOfEachIn(review.Committed.get());
			cache.FileCounts[IndexOf(ReviewGroup::Uncommitted)] = LineCountsOfEachIn(review.Uncommitted.get());
			cache.GroupCounts[IndexOf(ReviewGroup::Commits)] = SumOf(cache.FileCounts[IndexOf(ReviewGroup::Commits)]);
			cache.GroupCounts[IndexOf(ReviewGroup::Uncommitted)] =
				SumOf(cache.FileCounts[IndexOf(ReviewGroup::Uncommitted)]);
			cache.Summary =
				ChangeSummary(review.Committed->Files.size(), cache.GroupCounts[IndexOf(ReviewGroup::Commits)]);

			FollowNewDiffs(tab, review);
		}

		tab.LastPaths[IndexOf(ReviewGroup::Commits)] = ListedPathsOf(input.Groups, cache.Rows, ReviewGroup::Commits);
		tab.LastPaths[IndexOf(ReviewGroup::Uncommitted)] =
			ListedPathsOf(input.Groups, cache.Rows, ReviewGroup::Uncommitted);
	}

	void ReviewWindows::FollowNewDiffs(Tab& tab, const LoadedReview& review)
	{
		const std::vector<std::filesystem::path> noPathsBefore;
		const std::vector<std::filesystem::path>& pathsBefore =
			tab.Selected.has_value() ? tab.LastPaths[IndexOf(tab.Selected->Group)] : noPathsBefore;

		tab.Selected = FollowSelection(tab.Selected, pathsBefore, GroupsOf(tab, review), tab.Rows.Rows);
	}

	void ReviewWindows::UpdateTexts(Tab& tab, ReviewModel& model, const LoadedReview& review,
									const Core::FileDiff& file)
	{
		Core::Ref<const FileTexts> texts;

		if (tab.Selected->Group == ReviewGroup::Commits)
		{
			model.RequestCommittedTexts(review.Range->Ends, file);
			texts = model.GetCommittedTexts(review.Range->Ends, file);
		}
		else
		{
			model.RequestUncommittedTexts(review.Key, file);
			texts = model.GetUncommittedTexts(review.Key, file);
		}

		HoldTexts(tab, texts);
	}

	void ReviewWindows::HoldTexts(Tab& tab, const Core::Ref<const FileTexts>& texts)
	{
		// Copying a whole file's tokens every frame would cost the frame.
		if (texts == tab.Texts)
		{
			return;
		}

		tab.Texts = texts;
		tab.Tokens = texts != nullptr ? texts->Tokens : FileTokens{};
	}

	void ReviewWindows::PickCommit(Tab& tab, const std::optional<Core::Oid>& commit)
	{
		if (tab.SelectedCommit == commit)
		{
			return;
		}

		tab.SelectedCommit = commit;
		tab.SelectedCommitFile.reset();

		// Built again even when the rows were built from this commit's diff - picked, left for another
		// whose diff never arrived, and picked again - so that its first file is picked once more.
		tab.CommitRows.Built = false;
	}

	void ReviewWindows::FollowCommits(Tab& tab, const ReviewRange& range)
	{
		tab.SelectedCommit = FollowCommit(tab.SelectedCommit, range.Commits);

		if (!tab.SelectedCommit.has_value())
		{
			tab.SelectedCommitFile.reset();
		}
	}

	void ReviewWindows::RefreshCommitRows(Tab& tab, const SessionSettings& settings, const Core::CommitSummary& commit,
										  const Core::Ref<const Core::Diff>& commitDiff)
	{
		RowsCache& cache = tab.CommitRows;
		const std::string_view filter{tab.Filter.data()};

		const bool diffChanged = !cache.Built || cache.Committed != commitDiff;
		const bool sameList = !diffChanged && cache.Filter == filter && cache.Shape == settings.Shape &&
							  cache.FoldsVersion == tab.FoldsInCommitVersion;

		if (sameList)
		{
			return;
		}

		const ReviewGroups groups{.Commits = commitDiff.get()};
		const ReviewTreeInput input{.Groups = groups,
									.Shape = settings.Shape,
									.Headings = GroupHeadings::Hidden,
									.Filter = std::string{filter},
									.HideViewed = false,
									.FoldedFolders = &tab.FoldedInCommit};

		cache.Rows = BuildReviewTree(input);
		cache.Filter = input.Filter;
		cache.Shape = settings.Shape;
		cache.FoldsVersion = tab.FoldsInCommitVersion;
		cache.Built = true;

		if (diffChanged)
		{
			cache.Committed = commitDiff;
			cache.FileCounts[IndexOf(ReviewGroup::Commits)] = LineCountsOfEach(*commitDiff);
			cache.Summary = CommitLine(commit, commitDiff.get());

			tab.SelectedCommitFile = FollowSelection(tab.SelectedCommitFile, tab.LastCommitPaths, groups, cache.Rows);

			// The list's child is the same one for every commit, so without this it keeps the scroll the
			// last commit's list was left at.
			tab.ScrollToCommitFile = true;
		}

		tab.LastCommitPaths = ListedPathsOf(groups, cache.Rows, ReviewGroup::Commits);
	}

	ReviewWindows::OpenRelativeFile ReviewWindows::OpenFileIn(const ReviewRange& range) const
	{
		if (!OpenFile || !range.Worktree.has_value())
		{
			return nullptr;
		}

		const std::filesystem::path worktree = *range.Worktree;
		return [this, worktree](const std::filesystem::path& relative) { OpenFile(worktree / relative); };
	}

	void ReviewWindows::RenderEndDialog(const ReviewRenderInput& input)
	{
		DialogLayout::BeforeBegin(DialogLayout::kNarrowWidth);

		const bool open = m_EndQuestion.Begin(
			[&input](const std::string& branch) { return input.Sessions.Find(branch) != nullptr; },
			[](const std::string& branch) { return "End the review of " + ShortBranchName(branch); });

		DialogLayout::AfterBegin();

		if (!open)
		{
			return;
		}

		const std::string* current = m_EndQuestion.GetCurrent();

		if (current == nullptr)
		{
			ImGui::CloseCurrentPopup();
			m_EndQuestion.End();
			return;
		}

		const std::string branch = *current;

		DialogLayout::Text("End the review of " + ShortBranchName(branch) + "? Its viewed marks are deleted with it.");
		DialogLayout::Gap();
		DialogLayout::MutedText("Ending a review cannot be undone.");
		DialogLayout::Gap();

		const DialogLayout::ButtonSpec noSafeChoice;
		const DialogLayout::Choice choice =
			DialogLayout::ButtonRow(noSafeChoice, {.Label = "End review"}, input.Danger);

		if (choice == DialogLayout::Choice::Destructive)
		{
			const ReviewSession* session = input.Sessions.Find(branch);

			if (session != nullptr)
			{
				input.Model.Forget(ReviewKey{.Branch = branch, .Base = session->Base});
			}

			input.Sessions.End(branch);
			FollowSessions(input.Sessions, input.Model);
			m_EndQuestion.Answer();
		}
		else if (choice == DialogLayout::Choice::Cancel)
		{
			m_EndQuestion.Answer();
		}

		m_EndQuestion.End();
	}

	void ReviewWindows::Render(const ReviewRenderInput& input)
	{
		FollowSessions(input.Sessions, input.Model);

		for (std::size_t tabIndex = 0; tabIndex < m_Tabs.size();)
		{
			Tab& tab = m_Tabs[tabIndex];
			const std::string name = ReviewWindowName(tab.Branch);

			// Docked only on the frame its identity is new, as the diff tabs are: docking every frame
			// would fight a reader who dragged the tab elsewhere.
			bool restored = false;

			if (tab.Fresh)
			{
				tab.Fresh = false;
				restored = std::exchange(tab.Restored, false);

				const bool placeIsRemembered = restored && HasRememberedPlace(name);

				if (input.CentreNode != 0 && !placeIsRemembered)
				{
					ImGui::DockBuilderDockWindow(name.c_str(), input.CentreNode);
				}
			}

			if (std::exchange(tab.Focus, false))
			{
				ImGui::SetNextWindowFocus();
			}

			bool open = true;

			// A restored window must not take focus on the frame it appears, or the last one drawn
			// would take the front of its dock node from whatever imgui.ini remembers there.
			const ImGuiWindowFlags windowFlags = restored ? ImGuiWindowFlags_NoFocusOnAppearing : ImGuiWindowFlags_None;

			// Begin returning false still needs its End.
			const bool visible = ImGui::Begin(name.c_str(), &open, windowFlags);

			if (input.CloseRequests.Take(name))
			{
				open = false;
			}

			Core::DrawTabContextMenu(input.CloseRequests);

			if (visible)
			{
				RenderTab(tab, input);
			}

			ImGui::End();

			if (!open || tab.CloseRequested)
			{
				ForgetTab(tab, input.Model);
				m_Tabs.erase(m_Tabs.begin() + static_cast<std::ptrdiff_t>(tabIndex));
				continue;
			}

			++tabIndex;
		}

		RenderEndDialog(input);
	}
}
