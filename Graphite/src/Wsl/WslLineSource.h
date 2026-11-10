#pragma once

#include "Git/Lines/LineSource.h"
#include "Wsl/WslGit.h"

namespace Graphite {

	// The lines graph's reads of a repository inside WSL, asked of its distro's agent.
	class WslLineSource final : public LineSource
	{
	public:
		explicit WslLineSource(WslGit& git) : m_Git(git) {}

		Core::Result<std::vector<Core::Branch>> QueryBranches() override { return m_Git.QueryBranches(); }
		Core::Result<std::vector<Core::Remote>> QueryRemotes() override { return m_Git.QueryRemotes(); }
		Core::Result<std::unordered_set<Core::Oid>> ShallowBoundary() override { return m_Git.ShallowBoundary(); }
		Core::Result<std::vector<bool>> HoldsCommits(const std::vector<Core::Oid>& commits) override { return m_Git.HoldsCommits(commits); }
		Core::Result<std::vector<Core::GraphCommit>> ReadGraphCommits(const Core::GraphReadRequest& request) override
		{
			return m_Git.ReadGraphCommits(request);
		}

	private:
		WslGit& m_Git;
	};
}
