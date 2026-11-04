#include "Git/Worktrees/WorktreesModel.h"

#include "Workspace/Workspace.h"

#include <string>
#include <utility>

namespace Graphite {

	namespace {

		constexpr std::string_view kLocalBranchPrefix = "refs/heads/";

		bool HoldsBranch(const Core::WorktreeEntry& entry)
		{
			return !entry.FolderMissing && entry.Head.Kind == Core::HeadKind::Branch && !entry.Head.BranchName.empty();
		}
	}

	WorktreesModel::WorktreesModel(WorktreePort& port, Core::JobSubmitter submit)
		: m_Port(port), m_Submit(std::move(submit))
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
		m_Counting.clear();
		m_SinceCount = kPollSeconds;
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
			if (!HoldsBranch(entry))
			{
				continue;
			}

			m_WorktreeOfBranch.emplace(std::string(kLocalBranchPrefix) + entry.Head.BranchName, entry.Path);
			present.insert(Core::PathKey(entry.Path));
		}

		std::erase_if(m_Dirty, [&present](const auto& counted) { return !present.contains(counted.first); });
		m_SinceCount = kPollSeconds;
	}

	void WorktreesModel::SetOnScreen(bool onScreen)
	{
		if (onScreen && !m_OnScreen)
		{
			m_SinceCount = kPollSeconds;
		}

		m_OnScreen = onScreen;
	}

	void WorktreesModel::OnUpdate(Core::Timestep timestep)
	{
		m_Inbox.Drain();

		if (!m_OnScreen)
		{
			return;
		}

		m_SinceCount += timestep.GetSeconds();

		if (m_SinceCount >= kPollSeconds)
		{
			m_SinceCount = 0.0f;
			AskCounts();
		}
	}

	void WorktreesModel::AskCounts()
	{
		const Core::Ref<const std::vector<Core::WorktreeEntry>> entries = m_Entries.Get();

		if (entries == nullptr)
		{
			return;
		}

		for (const Core::WorktreeEntry& entry : *entries)
		{
			if (!HoldsBranch(entry) || IsOpenFolder(entry.Path))
			{
				continue;
			}

			const Core::PathKeyText key = Core::PathKey(entry.Path);

			if (!m_Counting.insert(key).second)
			{
				continue;
			}

			m_Submit([this, key, path = entry.Path, folder = m_Folder.load()]() {
				if (!IsSameFolder(folder))
				{
					return;
				}

				Core::Result<std::size_t> count = m_Port.CountUncommitted(path);
				std::optional<bool> dirty;

				if (count.HasValue())
				{
					dirty = count.GetValue() > 0;
				}

				m_Inbox.Post([this, key, folder, dirty]() {
					if (!IsSameFolder(folder))
					{
						return;
					}

					m_Counting.erase(key);

					if (dirty.has_value())
					{
						m_Dirty[key] = *dirty;
					}
				});
			});
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
