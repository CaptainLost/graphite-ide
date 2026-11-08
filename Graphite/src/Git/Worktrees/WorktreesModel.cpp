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
		m_LastAsked.reset();
		m_WorktreeOfBranch.clear();
		m_Dirty.clear();
		m_Failures.clear();
		m_Timing.clear();
		m_Observed.clear();
		m_Listed.clear();
		m_NextSweep = m_Clock + kSweepSeconds;
		m_FirstCountWaited = 0.0f;
		m_Counting.clear();
		m_InView.clear();
	}

	void WorktreesModel::Follow(const Core::Ref<const std::vector<Core::Branch>>& branches, const std::filesystem::path& openFolder)
	{
		m_OpenFolder = openFolder;
		m_OpenFolderKey = openFolder.empty() ? Core::PathKeyText{} : Core::PathKey(openFolder);

		if (branches == nullptr || branches == m_LastAsked)
		{
			return;
		}

		m_LastAsked = branches;

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
		if (m_Entries.Land(generation, std::move(result)) != SlotLanding::Applied)
		{
			return;
		}

		m_WorktreeOfBranch.clear();
		std::set<Core::PathKeyText> present;

		for (const Core::WorktreeEntry& entry : *m_Entries.Get())
		{
			present.insert(Core::PathKey(entry.Path));

			if (HoldsBranch(entry))
			{
				m_WorktreeOfBranch.emplace(std::string(kLocalBranchPrefix) + entry.Head.BranchName, entry.Path);
			}
		}

		m_FirstCountWaited = 0.0f;
		ForgetWorktreesNotListed(m_Observed, present);
		ForgetWorktreesNotListed(m_Dirty, present);
		ForgetWorktreesNotListed(m_Failures, present);
		ForgetWorktreesNotListed(m_Timing, present);
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

		const std::set<Core::PathKeyText> inView = std::exchange(m_InView, {});

		if (!m_OnScreen)
		{
			return;
		}

		m_Clock += timestep.GetSeconds();
		m_FirstCountWaited += timestep.GetSeconds();
		AskCounts(inView);
		AskSweep(inView);
	}

	bool WorktreesModel::IsCountable(const Core::WorktreeEntry& entry) const
	{
		return !entry.FolderMissing && !IsOpenFolder(entry.Path);
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

			Core::Result<WorktreeActivity> activity = m_Port.QueryActivity(path);
			Core::Result<WorktreeCount> count = m_Port.CountUncommitted(path);

			m_Inbox.Post([this, key, folder, activity = std::move(activity), count = std::move(count)]() {
				if (IsSameFolder(folder))
				{
					LandCount(key, activity, count);
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

		const Core::WorktreeEntry* oldest = nullptr;
		float oldestLanded = 0.0f;

		for (const Core::WorktreeEntry& entry : *entries)
		{
			const Core::PathKeyText key = Core::PathKey(entry.Path);
			const auto observed = m_Observed.find(key);

			if (!IsCountable(entry) || inView.contains(key) || key == m_LookedAt || m_Counting.contains(key) ||
				observed == m_Observed.end() || !observed->second.Counted)
			{
				continue;
			}

			if (oldest == nullptr || observed->second.LandedAt < oldestLanded)
			{
				oldest = &entry;
				oldestLanded = observed->second.LandedAt;
			}
		}

		if (oldest == nullptr)
		{
			return;
		}

		m_NextSweep = m_Clock + kSweepSeconds;
		AskCount(Core::PathKey(oldest->Path), oldest->Path);
	}

	void WorktreesModel::AskCounts(const std::set<Core::PathKeyText>& inView)
	{
		const Core::Ref<const std::vector<Core::WorktreeEntry>> entries = m_Entries.Get();

		if (entries == nullptr)
		{
			return;
		}

		for (const Core::WorktreeEntry& entry : *entries)
		{
			if (!IsCountable(entry))
			{
				continue;
			}

			const Core::PathKeyText key = Core::PathKey(entry.Path);
			const std::map<Core::PathKeyText, CountTiming>::const_iterator timing = m_Timing.find(key);
			const std::map<Core::PathKeyText, Observed>::const_iterator observed = m_Observed.find(key);
			const bool firstCount = observed == m_Observed.end() || !observed->second.Counted;
			const bool isDue = timing == m_Timing.end() || m_Clock >= timing->second.Due;
			const bool polled = inView.contains(key) && key != m_LookedAt && isDue;

			if (firstCount || polled)
			{
				AskCount(key, entry.Path);
			}
		}
	}

	void WorktreesModel::LandCount(const Core::PathKeyText& key, const Core::Result<WorktreeActivity>& activity,
								   const Core::Result<WorktreeCount>& count)
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

		if (activity.HasValue())
		{
			const WorktreeActivity& read = activity.GetValue();
			observed.Activity = std::max({read.Reflog, read.Index, read.HeadCommit});
		}

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

	std::optional<std::size_t> WorktreesModel::CountOf(const std::filesystem::path& worktree) const
	{
		const auto found = m_Observed.find(Core::PathKey(worktree));
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

	bool WorktreesModel::IsFirstCountSettled() const
	{
		const Core::Ref<const std::vector<Core::WorktreeEntry>> entries = m_Entries.Get();

		if (entries == nullptr)
		{
			return false;
		}

		if (m_FirstCountWaited >= kFirstCountCapSeconds)
		{
			return true;
		}

		return std::ranges::all_of(*entries, [this](const Core::WorktreeEntry& entry) {
			const auto observed = m_Observed.find(Core::PathKey(entry.Path));
			return !IsCountable(entry) || (observed != m_Observed.end() && observed->second.Counted);
		});
	}

	void WorktreesModel::SetLookedAt(std::optional<std::filesystem::path> worktree)
	{
		m_LookedAt = worktree.has_value() ? Core::PathKey(*worktree) : Core::PathKeyText{};
	}
}
