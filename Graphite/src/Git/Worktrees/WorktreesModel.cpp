#include "Git/Worktrees/WorktreesModel.h"

#include "Workspace/Workspace.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::string_view kLocalBranchPrefix = "refs/heads/";

		bool HoldsBranch(const Core::WorktreeEntry& entry)
		{
			return !entry.FolderMissing && entry.Head.Kind == Core::HeadKind::Branch && !entry.Head.BranchName.empty();
		}

		bool IsNotListed(const Core::PathKeyText& worktree, const std::set<Core::PathKeyText>& listed)
		{
			return !listed.contains(worktree);
		}

		// Drops what is held about a worktree the newest listing no longer has.
		template<typename Held>
		void ForgetWorktreesNotListed(std::map<Core::PathKeyText, Held>& byWorktree, const std::set<Core::PathKeyText>& listed)
		{
			std::erase_if(byWorktree, [&listed](const std::pair<const Core::PathKeyText, Held>& held) { return IsNotListed(held.first, listed); });
		}
	}

	WorktreesModel::WorktreesModel(WorktreePort& port, Core::JobSubmitter submit, std::function<std::int64_t()> nowSeconds)
		: m_Port(port), m_Submit(std::move(submit)), m_NowSeconds(std::move(nowSeconds))
	{
	}

	void WorktreesModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent&) { Clear(); });
		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) { Clear(); });
	}

	bool WorktreesModel::IsSameFolder(std::uint64_t folder) const
	{
		return folder == m_Folder.load();
	}

	bool WorktreesModel::IsOpenFolder(const std::filesystem::path& worktree) const
	{
		return !m_OpenFolderKey.empty() && Core::PathKey(worktree) == m_OpenFolderKey;
	}

	void WorktreesModel::Clear()
	{
		++m_Folder;

		m_Entries.Clear();
		m_EntryKeys.clear();
		++m_EntriesGeneration;
		m_ListingFollowed.reset();
		m_WorktreeOfBranch.clear();
		m_Dirty.clear();
		m_Failures.clear();
		m_Timing.clear();
		m_Observed.clear();
		m_Listed.clear();
		m_NextSweep = m_Clock + kSweepSeconds;
		m_FirstCountWaited = 0.0f;
		m_FirstCountSettled = false;
		m_Counting.clear();
		m_ReadingActivity.clear();
		m_LastRecount.clear();
		m_RecountDue.clear();
		m_InView.clear();
	}

	void WorktreesModel::Follow(const Core::Ref<const std::vector<Core::Branch>>& branches, std::uint64_t branchListing,
								const std::filesystem::path& openFolder)
	{
		// Keyed only when the folder's spelling changes: this is asked every frame.
		if (openFolder.native() != m_OpenFolder.native())
		{
			m_OpenFolder = openFolder;
			m_OpenFolderKey = openFolder.empty() ? Core::PathKeyText{} : Core::PathKey(openFolder);
		}

		if (branches == nullptr || m_ListingFollowed == branchListing)
		{
			return;
		}

		m_ListingFollowed = branchListing;
		AskList();
	}

	void WorktreesModel::ListAgain()
	{
		// Nothing is asked before the folder's first list: Follow asks that one.
		if (!m_ListingFollowed.has_value())
		{
			return;
		}

		AskList();
	}

	void WorktreesModel::AskList()
	{
		const std::uint64_t generation = m_Entries.Begin();
		const std::uint64_t folder = m_Folder.load();

		m_Submit([this, generation, folder]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			Core::Result<std::vector<Core::WorktreeEntry>> result = m_Port.QueryWorktrees();

			m_Inbox.Post([this, generation, folder, result = std::move(result)]() mutable {
				if (IsSameFolder(folder))
				{
					Land(generation, std::move(result));
				}
			});
		});
	}

	void WorktreesModel::Land(std::uint64_t generation, Core::Result<std::vector<Core::WorktreeEntry>> result)
	{
		// The same list read again keeps the one held, and its generation: every branch listing reads it, and what is
		// built from it is built again only for another.
		const Core::Ref<const std::vector<Core::WorktreeEntry>> held = m_Entries.Get();
		const bool sameAsHeld = result.HasValue() && held != nullptr && result.GetValue() == *held;
		const SlotLanding landing = sameAsHeld ? m_Entries.LandSameAsHeld(generation) : m_Entries.Land(generation, std::move(result));

		if (landing == SlotLanding::Stale || sameAsHeld)
		{
			return;
		}

		++m_EntriesGeneration;

		if (landing != SlotLanding::Applied)
		{
			return;
		}

		m_WorktreeOfBranch.clear();
		m_EntryKeys.clear();
		std::set<Core::PathKeyText> present;

		for (const Core::WorktreeEntry& entry : *m_Entries.Get())
		{
			m_EntryKeys.push_back(Core::PathKey(entry.Path));
			present.insert(m_EntryKeys.back());

			if (HoldsBranch(entry))
			{
				m_WorktreeOfBranch.emplace(std::string(kLocalBranchPrefix) + entry.Head.BranchName, entry.Path);
			}
		}

		ForgetWorktreesNotListed(m_Observed, present);
		ForgetWorktreesNotListed(m_Dirty, present);
		ForgetWorktreesNotListed(m_Failures, present);
		ForgetWorktreesNotListed(m_Timing, present);
		ForgetWorktreesNotListed(m_LastRecount, present);
		ForgetWorktreesNotListed(m_RecountDue, present);
		m_Listed = std::move(present);
	}

	void WorktreesModel::SetOnScreen(bool onScreen)
	{
		// Back on screen, every count is due at once.
		if (onScreen && !m_OnScreen)
		{
			for (auto& [key, timing] : m_Timing)
			{
				timing.Due = m_Clock;
			}
		}

		m_OnScreen = onScreen;
	}

	void WorktreesModel::OnUpdate(Core::Timestep timestep)
	{
		m_Inbox.Drain();
		SettleFirstCountWhenDone();

		const std::set<Core::PathKeyText> inView = std::exchange(m_InView, {});

		if (!m_OnScreen)
		{
			return;
		}

		m_Clock += timestep.GetSeconds();

		// WARNING: From the folder's first list on, never from the last: a list read again for every branch snapshot
		// would otherwise put the cap off for as long as the branches keep moving.
		if (m_Entries.Get() != nullptr)
		{
			m_FirstCountWaited += timestep.GetSeconds();
		}

		AskCounts(inView);
		AskSweep(inView);
		AskRecountsDue();
		SettleFirstCountWhenDone();
	}

	void WorktreesModel::SettleFirstCountWhenDone()
	{
		const Core::Ref<const std::vector<Core::WorktreeEntry>> entries = m_Entries.Get();

		if (m_FirstCountSettled || entries == nullptr)
		{
			return;
		}

		bool everyOneCounted = true;

		for (std::size_t entry = 0; entry < entries->size() && everyOneCounted; ++entry)
		{
			const auto observed = m_Observed.find(m_EntryKeys[entry]);
			everyOneCounted = !IsCountable(entry) || (observed != m_Observed.end() && observed->second.Counted);
		}

		m_FirstCountSettled = m_FirstCountWaited >= kFirstCountCapSeconds || everyOneCounted;
	}

	bool WorktreesModel::IsCountable(std::size_t entry) const
	{
		return !(*Entries())[entry].FolderMissing && (m_OpenFolderKey.empty() || m_EntryKeys[entry] != m_OpenFolderKey);
	}

	std::optional<std::size_t> WorktreesModel::IndexOfKey(const Core::PathKeyText& key) const
	{
		const std::vector<Core::PathKeyText>::const_iterator found = std::ranges::find(m_EntryKeys, key);

		if (found == m_EntryKeys.end())
		{
			return std::nullopt;
		}

		return static_cast<std::size_t>(found - m_EntryKeys.begin());
	}

	void WorktreesModel::AskReads(const Core::PathKeyText& key, const std::filesystem::path& path)
	{
		AskActivity(key, path);
		AskCount(key, path);
	}

	void WorktreesModel::AskActivity(const Core::PathKeyText& key, const std::filesystem::path& path)
	{
		if (!m_ReadingActivity.insert(key).second)
		{
			return;
		}

		m_Submit([this, key, path, folder = m_Folder.load()]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			Core::Result<WorktreeActivity> activity = m_Port.QueryActivity(path);

			m_Inbox.Post([this, key, folder, activity = std::move(activity)]() {
				if (IsSameFolder(folder))
				{
					LandActivity(key, activity);
				}
			});
		});
	}

	void WorktreesModel::AskCount(const Core::PathKeyText& key, const std::filesystem::path& path)
	{
		if (!m_Counting.insert(key).second)
		{
			return;
		}

		m_Submit([this, key, path, folder = m_Folder.load()]() {
			if (!IsSameFolder(folder))
			{
				return;
			}

			Core::Result<WorktreeCount> count = m_Port.CountUncommitted(path);

			m_Inbox.Post([this, key, folder, count = std::move(count)]() {
				if (IsSameFolder(folder))
				{
					LandCount(key, count);
				}
			});
		});
	}

	void WorktreesModel::AskSweep(const std::set<Core::PathKeyText>& inView)
	{
		const Core::Ref<const std::vector<Core::WorktreeEntry>> entries = m_Entries.Get();

		if (entries == nullptr || m_Clock < m_NextSweep)
		{
			return;
		}

		std::optional<std::size_t> oldest;
		float oldestLanded = 0.0f;

		for (std::size_t entry = 0; entry < entries->size(); ++entry)
		{
			const Core::PathKeyText& key = m_EntryKeys[entry];
			const auto observed = m_Observed.find(key);

			if (!IsCountable(entry) || inView.contains(key) || key == m_LookedAt || m_Counting.contains(key) ||
				observed == m_Observed.end() || !observed->second.Counted)
			{
				continue;
			}

			if (!oldest.has_value() || observed->second.LandedAt < oldestLanded)
			{
				oldest = entry;
				oldestLanded = observed->second.LandedAt;
			}
		}

		if (!oldest.has_value())
		{
			return;
		}

		m_NextSweep = m_Clock + kSweepSeconds;
		AskReads(m_EntryKeys[*oldest], (*entries)[*oldest].Path);
	}

	void WorktreesModel::AskCounts(const std::set<Core::PathKeyText>& inView)
	{
		const Core::Ref<const std::vector<Core::WorktreeEntry>> entries = m_Entries.Get();

		if (entries == nullptr)
		{
			return;
		}

		std::vector<std::size_t> due;

		for (std::size_t entry = 0; entry < entries->size(); ++entry)
		{
			const Core::PathKeyText& key = m_EntryKeys[entry];

			if (!IsCountable(entry) || m_Counting.contains(key))
			{
				continue;
			}

			const std::map<Core::PathKeyText, CountTiming>::const_iterator timing = m_Timing.find(key);
			const std::map<Core::PathKeyText, Observed>::const_iterator observed = m_Observed.find(key);
			const bool firstCount = observed == m_Observed.end() || !observed->second.Counted;
			const bool isDue = timing == m_Timing.end() || m_Clock >= timing->second.Due;
			const bool polled = inView.contains(key) && key != m_LookedAt && isDue;

			if (firstCount || polled)
			{
				due.push_back(entry);
			}
		}

		// WARNING: Every activity read goes ahead of every count. A worker then lands the cheap reads first, and a
		// worktree whose count is held past the first build's cap is placed by its activity, not after every other.
		for (const std::size_t entry : due)
		{
			AskActivity(m_EntryKeys[entry], (*entries)[entry].Path);
		}

		for (const std::size_t entry : due)
		{
			AskCount(m_EntryKeys[entry], (*entries)[entry].Path);
		}
	}

	void WorktreesModel::AskRecountsDue()
	{
		const Core::Ref<const std::vector<Core::WorktreeEntry>> entries = m_Entries.Get();

		if (entries == nullptr)
		{
			return;
		}

		for (std::map<Core::PathKeyText, float>::iterator recount = m_RecountDue.begin(); recount != m_RecountDue.end();)
		{
			const Core::PathKeyText key = recount->first;

			// A count already on its way may have read the worktree before its last change: this one waits for it.
			if (m_Clock < recount->second || m_Counting.contains(key))
			{
				++recount;
				continue;
			}

			recount = m_RecountDue.erase(recount);

			const std::optional<std::size_t> entry = IndexOfKey(key);

			if (entry.has_value() && IsCountable(*entry))
			{
				m_LastRecount[key] = m_Clock;
				AskReads(key, (*entries)[*entry].Path);
			}
		}
	}

	void WorktreesModel::ListAgainWhenGone(const Core::Error& error)
	{
		// The activity read and the count of a worktree that went both find nothing: one list answers both.
		if (error.Code == Core::ErrorCode::NotFound && !m_Entries.IsInFlight())
		{
			ListAgain();
		}
	}

	void WorktreesModel::LandActivity(const Core::PathKeyText& key, const Core::Result<WorktreeActivity>& activity)
	{
		m_ReadingActivity.erase(key);

		if (!m_Listed.contains(key))
		{
			return;
		}

		if (!activity.HasValue())
		{
			ListAgainWhenGone(activity.GetError());
			return;
		}

		const WorktreeActivity& read = activity.GetValue();
		m_Observed[key].Activity = std::max({read.Reflog, read.Index, read.HeadCommit});
	}

	void WorktreesModel::LandCount(const Core::PathKeyText& key, const Core::Result<WorktreeCount>& count)
	{
		m_Counting.erase(key);

		if (!m_Listed.contains(key))
		{
			return;
		}

		Observed& observed = m_Observed[key];
		observed.Counted = true;
		observed.LandedAt = m_Clock;
		observed.Count = count.HasValue() ? std::optional<std::size_t>{count.GetValue().Changed} : std::nullopt;

		if (count.HasValue())
		{
			observed.NewestChange = count.GetValue().NewestChange;
		}

		CountTiming& timing = m_Timing[key];
		const std::optional<std::size_t> landed = count.HasValue() ? std::optional<std::size_t>{count.GetValue().Changed} : std::nullopt;

		// A worktree left alone, or failing again, is asked less and less often; one that changed, or answered after
		// failing, as often as at first.
		const bool failedBefore = m_Failures.contains(key);
		const bool unchanged = landed.has_value() ? timing.Last == landed : failedBefore;
		timing.Interval = unchanged ? std::min(timing.Interval * 2.0f, kLongestPollSeconds) : kPollSeconds;
		timing.Due = m_Clock + timing.Interval;
		timing.Last = landed;

		if (landed.has_value())
		{
			m_Dirty[key] = *landed > 0;
			m_Failures.erase(key);
		}
		else
		{
			m_Dirty.erase(key);
			m_Failures[key] = count.GetError().Message;
			ListAgainWhenGone(count.GetError());
		}
	}

	std::optional<std::filesystem::path> WorktreesModel::WorktreeOf(std::string_view branchFullName) const
	{
		const auto found = m_WorktreeOfBranch.find(branchFullName);

		if (found == m_WorktreeOfBranch.end())
		{
			return std::nullopt;
		}

		return found->second;
	}

	std::optional<std::filesystem::path> WorktreesModel::OtherWorktreeOf(std::string_view branchFullName) const
	{
		std::optional<std::filesystem::path> worktree = WorktreeOf(branchFullName);

		if (worktree.has_value() && IsOpenFolder(*worktree))
		{
			return std::nullopt;
		}

		return worktree;
	}

	std::optional<bool> WorktreesModel::DirtyInView(const std::filesystem::path& worktree)
	{
		m_InView.insert(Core::PathKey(worktree));
		return IsDirty(worktree);
	}

	std::optional<std::string> WorktreesModel::CountFailureOf(const std::filesystem::path& worktree) const
	{
		const auto found = m_Failures.find(Core::PathKey(worktree));

		if (found == m_Failures.end())
		{
			return std::nullopt;
		}

		return found->second;
	}

	std::optional<bool> WorktreesModel::IsDirty(const std::filesystem::path& worktree) const
	{
		const auto found = m_Dirty.find(Core::PathKey(worktree));

		if (found == m_Dirty.end())
		{
			return std::nullopt;
		}

		return found->second;
	}
}

namespace Graphite {

	const std::vector<Core::WorktreeEntry>* WorktreesModel::Entries() const
	{
		return m_Entries.Get().get();
	}

	std::optional<std::size_t> WorktreesModel::IndexOf(const std::filesystem::path& worktree) const
	{
		return IndexOfKey(Core::PathKey(worktree));
	}

	std::optional<std::size_t> WorktreesModel::CountOf(const std::filesystem::path& worktree) const
	{
		return CountOfKey(Core::PathKey(worktree));
	}

	std::optional<std::size_t> WorktreesModel::CountOfKey(const Core::PathKeyText& worktree) const
	{
		const auto found = m_Observed.find(worktree);
		return found == m_Observed.end() ? std::nullopt : found->second.Count;
	}

	std::optional<std::chrono::sys_seconds> WorktreesModel::LastActivity(const std::filesystem::path& worktree) const
	{
		const auto found = m_Observed.find(Core::PathKey(worktree));

		if (found == m_Observed.end())
		{
			return std::nullopt;
		}

		return std::max(found->second.Activity, found->second.NewestChange);
	}

	bool WorktreesModel::IsLive(const std::filesystem::path& worktree) const
	{
		const std::optional<std::chrono::sys_seconds> last = LastActivity(worktree);
		return last.has_value() && m_NowSeconds() - last->time_since_epoch().count() <= kLiveSeconds;
	}

	void WorktreesModel::SetLookedAt(std::optional<std::filesystem::path> worktree)
	{
		m_LookedAt = worktree.has_value() ? Core::PathKey(*worktree) : Core::PathKeyText{};
	}

	void WorktreesModel::RecountNow(const std::filesystem::path& worktree)
	{
		const Core::Ref<const std::vector<Core::WorktreeEntry>> entries = m_Entries.Get();

		if (entries == nullptr)
		{
			return;
		}

		const Core::PathKeyText key = Core::PathKey(worktree);
		const std::optional<std::size_t> entry = IndexOfKey(key);

		// Already put off: that recount comes after this change too.
		if (!entry.has_value() || !IsCountable(*entry) || m_RecountDue.contains(key))
		{
			return;
		}

		const std::map<Core::PathKeyText, float>::const_iterator last = m_LastRecount.find(key);

		if (last != m_LastRecount.end() && m_Clock < last->second + kPollSeconds)
		{
			m_RecountDue[key] = last->second + kPollSeconds;
			return;
		}

		if (m_Counting.contains(key))
		{
			m_RecountDue[key] = m_Clock;
			return;
		}

		m_LastRecount[key] = m_Clock;
		AskReads(key, (*entries)[*entry].Path);
	}
}
