#pragma once

#include "Git/Worktrees/WorktreePort.h"

#include "Core/Git/Worktree.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite::Fixtures {

	inline Core::WorktreeEntry WorktreeOn(std::string branch, std::filesystem::path path)
	{
		Core::WorktreeEntry entry;
		entry.Path = std::move(path);
		entry.Head.Kind = Core::HeadKind::Branch;
		entry.Head.BranchName = std::move(branch);
		return entry;
	}

	// Answers what a test set, by the worktree's generic path, and records what was asked.
	class FakeWorktreePort final : public WorktreePort
	{
	public:
		Core::Result<std::vector<Core::WorktreeEntry>> QueryWorktrees() override
		{
			++Listed;
			return Entries;
		}

		Core::Result<WorktreeActivity> QueryActivity(const std::filesystem::path& worktree) override
		{
			ActivityQueried.push_back(worktree);

			if (ActivityFailure.has_value())
			{
				return *ActivityFailure;
			}

			return Activity[worktree.generic_string()];
		}

		Core::Result<WorktreeCount> CountUncommitted(const std::filesystem::path& worktree) override
		{
			Counted.push_back(worktree);

			if (CountFailure.has_value())
			{
				return *CountFailure;
			}

			if (FailCounts)
			{
				return Core::Error{Core::ErrorCode::IoFailure, "the index is locked"};
			}

			WorktreeCount count;
			count.Changed = Changes[worktree.generic_string()];
			count.NewestChange = Newest[worktree.generic_string()];
			return count;
		}

		std::vector<Core::WorktreeEntry> Entries;
		std::map<std::string, std::size_t> Changes;
		std::map<std::string, WorktreeActivity> Activity;
		std::map<std::string, std::optional<std::chrono::sys_seconds>> Newest;
		std::vector<std::filesystem::path> ActivityQueried;
		bool FailCounts = false;
		std::optional<Core::Error> CountFailure;    // set, every count answers it
		std::optional<Core::Error> ActivityFailure; // set, every activity read answers it
		int Listed = 0;
		std::vector<std::filesystem::path> Counted;
	};
}
