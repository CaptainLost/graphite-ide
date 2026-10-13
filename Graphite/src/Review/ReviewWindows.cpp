#include "Review/ReviewWindows.h"

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

	std::optional<ReviewKey> ReviewWindows::KeyOf(const Tab& tab)
	{
		if (!tab.Target.Base.has_value())
		{
			return std::nullopt;
		}

		return ReviewKey{.Branch = tab.Target.Branch, .Base = *tab.Target.Base};
	}

	void ReviewWindows::ForgetTab(const Tab& tab, ReviewModel& model)
	{
		const std::optional<ReviewKey> key = KeyOf(tab);

		if (key.has_value())
		{
			model.Forget(*key);
		}
	}

	void ReviewWindows::Open(const ReviewTarget& target)
	{
		for (Tab& tab : m_Tabs)
		{
			if (tab.Target.Branch == target.Branch)
			{
				tab.Focus = true;
				return;
			}
		}

		Tab tab;
		tab.Target = target;
		m_Tabs.push_back(std::move(tab));
	}

	bool ReviewWindows::IsOpen(const std::string& branchFullName) const
	{
		for (const Tab& tab : m_Tabs)
		{
			if (tab.Target.Branch == branchFullName)
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
	}

	std::vector<ReviewTabState> ReviewWindows::CaptureState() const
	{
		std::vector<ReviewTabState> reviews;
		reviews.reserve(m_Tabs.size());

		for (const Tab& tab : m_Tabs)
		{
			ReviewTabState review{.Branch = tab.Target.Branch,
								  .Base = tab.Target.Base,
								  .IncludeUncommitted = tab.IncludeUncommitted};

			if (tab.Selected.has_value())
			{
				review.Selected = tab.Selected->Path;
				review.SelectedIsUncommitted = tab.Selected->Group == ReviewGroup::Uncommitted;
			}

			reviews.push_back(std::move(review));
		}

		return reviews;
	}

	void ReviewWindows::RestoreState(const std::vector<ReviewTabState>& reviews)
	{
		for (const ReviewTabState& stored : reviews)
		{
			if (IsOpen(stored.Branch))
			{
				continue;
			}

			Tab tab;
			tab.Target = ReviewTarget{.Branch = stored.Branch, .Base = stored.Base};
			tab.IncludeUncommitted = stored.IncludeUncommitted;

			const bool selectionCanShow =
				!stored.Selected.empty() && (stored.IncludeUncommitted || !stored.SelectedIsUncommitted);

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

			for (Tab& tab : m_Tabs)
			{
				if (tab.Target.Base == rename.From)
				{
					FollowBaseRename(tab, model, rename.To);
				}
			}

			for (std::size_t tabIndex = 0; tabIndex < m_Tabs.size();)
			{
				Tab& tab = m_Tabs[tabIndex];

				if (tab.Target.Branch != rename.From)
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

				tab.Target.Branch = rename.To;
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

			if (ListsBranchNamed(branches, tab.Target.Branch))
			{
				++tabIndex;
				continue;
			}

			APP_INFO("Not reopening the review of '{}': that branch is gone", tab.Target.Branch);
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

	void ReviewWindows::ChangeBase(Tab& tab, ReviewModel& model, const std::string& base)
	{
		ForgetTab(tab, model);
		tab.Target.Base = base;
		tab.Selected.reset();
		tab.LastPaths = {};
		tab.Rows = RowsCache{};
		tab.LastLoaded.reset();
		tab.LastWorktree.reset();
	}

	void ReviewWindows::FollowBaseRename(Tab& tab, ReviewModel& model, const std::string& renamedBase)
	{
		ForgetTab(tab, model);
		tab.Target.Base = renamedBase;
		tab.LastLoaded.reset();
	}

	void ReviewWindows::SetIncludeUncommitted(Tab& tab, ReviewModel& model, bool include)
	{
		tab.IncludeUncommitted = include;
		tab.Rows.Built = false;

		if (include)
		{
			return;
		}

		const std::optional<ReviewKey> key = KeyOf(tab);

		if (key.has_value())
		{
			model.StopUncommitted(*key);
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
		if (sessions.StateOf(tab.Target.Branch, group, file) == ViewedState::Viewed)
		{
			sessions.Unmark(tab.Target.Branch, group, file);
			return;
		}

		sessions.Mark(tab.Target.Branch, group, file);
	}

	void ReviewWindows::RefreshRows(Tab& tab, const LoadedReview& review)
	{
		const bool sameAnswers = tab.Rows.Built && tab.Rows.Committed == review.Committed &&
								 tab.Rows.Uncommitted == review.Uncommitted && tab.Rows.Collapsed == tab.Collapsed;

		if (sameAnswers)
		{
			return;
		}

		const ReviewGroups groups = GroupsOf(tab, review);
		std::vector<ReviewListRow> rows = BuildReviewRows(groups);
		std::vector<LineCounts> counts;
		counts.reserve(rows.size());

		for (const ReviewListRow& row : rows)
		{
			counts.push_back(row.IsHeading ? LineCounts{} : CountLines(FileOfRow(groups, row)));
		}

		const std::vector<std::filesystem::path> noPathsBefore;
		const std::vector<std::filesystem::path>& pathsBefore =
			tab.Selected.has_value() ? tab.LastPaths[IndexOf(tab.Selected->Group)] : noPathsBefore;

		tab.Selected = FollowSelection(tab.Selected, pathsBefore, groups, FirstListedFile(groups, rows));

		tab.LastPaths[IndexOf(ReviewGroup::Commits)] = ShownPathsByName(*review.Committed);
		tab.LastPaths[IndexOf(ReviewGroup::Uncommitted)] =
			review.Uncommitted != nullptr ? ShownPathsByName(*review.Uncommitted) : std::vector<std::filesystem::path>{};

		tab.Rows = RowsCache{.Committed = review.Committed,
							 .Uncommitted = review.Uncommitted,
							 .Collapsed = tab.Collapsed,
							 .Rows = std::move(rows),
							 .Counts = std::move(counts),
							 .Built = true};
	}

	ViewedCount ReviewWindows::ViewedCountOf(Tab& tab, const ReviewSessions& sessions, const ReviewGroups& groups)
	{
		const bool countIsCurrent = tab.Rows.ViewedCounted && tab.Rows.ViewedForVersion == sessions.GetVersion();

		if (!countIsCurrent)
		{
			const std::string& branch = tab.Target.Branch;
			tab.Rows.Viewed = CountViewed(groups, [&sessions, &branch](ReviewGroup group, const Core::FileDiff& file)
										  { return sessions.StateOf(branch, group, file); });
			tab.Rows.ViewedForVersion = sessions.GetVersion();
			tab.Rows.ViewedCounted = true;
		}

		return tab.Rows.Viewed;
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

		// Copied only when a different texts object arrives: copying a whole file's tokens every
		// frame would cost the frame.
		if (texts == tab.Texts)
		{
			return;
		}

		tab.Texts = texts;
		tab.Tokens = texts != nullptr ? texts->Tokens : FileTokens{};
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

	void ReviewWindows::Render(const ReviewRenderInput& input)
	{
		for (std::size_t tabIndex = 0; tabIndex < m_Tabs.size();)
		{
			Tab& tab = m_Tabs[tabIndex];
			const std::string name = ReviewWindowName(tab.Target.Branch);

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
	}
}
