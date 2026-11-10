#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Lines/AncestryGraph.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Remote.h"
#include "Core/Git/Repository.h"
#include "Core/Git/Requests.h"

#include <unordered_set>
#include <vector>

namespace Graphite {

	// What a lines read asks of git, and nothing else: the graph, its answers and its store stay
	// with the reader. A local handle answers here; a repository inside WSL answers through its
	// distro's agent.
	class LineSource
	{
	public:
		virtual ~LineSource() = default;

		virtual Core::Result<std::vector<Core::Branch>> QueryBranches() = 0;
		virtual Core::Result<std::vector<Core::Remote>> QueryRemotes() = 0;

		// The commits the shallow file names; none for a whole clone.
		virtual Core::Result<std::unordered_set<Core::Oid>> ShallowBoundary() = 0;

		// Whether git holds each commit, in the order asked: one question for all a read needs.
		virtual Core::Result<std::vector<bool>> HoldsCommits(const std::vector<Core::Oid>& commits) = 0;

		virtual Core::Result<std::vector<Core::GraphCommit>> ReadGraphCommits(const Core::GraphReadRequest& request) = 0;
	};

	// A handle opened on this machine.
	class RepositoryLineSource final : public LineSource
	{
	public:
		explicit RepositoryLineSource(const Core::Repository& repository) : m_Repository(repository) {}

		Core::Result<std::vector<Core::Branch>> QueryBranches() override { return m_Repository.QueryBranches(); }
		Core::Result<std::vector<Core::Remote>> QueryRemotes() override { return m_Repository.QueryRemotes(); }
		Core::Result<std::unordered_set<Core::Oid>> ShallowBoundary() override { return m_Repository.ShallowBoundary(); }
		Core::Result<std::vector<bool>> HoldsCommits(const std::vector<Core::Oid>& commits) override;
		Core::Result<std::vector<Core::GraphCommit>> ReadGraphCommits(const Core::GraphReadRequest& request) override
		{
			return m_Repository.ReadGraphCommits(request);
		}

	private:
		const Core::Repository& m_Repository;
	};
}
