#pragma once

#include "Core/Base/Result.h"
#include "Core/Git/Oid.h"
#include "Core/Git/PatchMatch.h"
#include "Core/Git/Worktree.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct BranchTip
	{
		std::string FullName;
		Core::Oid Tip;

		friend bool operator==(const BranchTip&, const BranchTip&) = default;
	};

	struct WorkFactsRequest
	{
		std::string TrunkFullName;
		Core::Oid TrunkTip;
		std::vector<BranchTip> Branches; // local branches other than the trunk, HEAD's included
		std::size_t PatchBudget = 0;

		friend bool operator==(const WorkFactsRequest&, const WorkFactsRequest&) = default;
	};

	struct BranchFacts
	{
		std::string FullName;
		std::optional<Core::PatchMatch> Match; // empty only with Failure
		std::optional<Core::Error> Failure;
	};

	struct WorktreeFacts
	{
		Core::WorktreeEntry Entry;

		// Empty for the open folder's own worktree (GitModel's status counts it), for a missing folder and for an
		// unreadable status. The main checkout is counted when a linked worktree is the open folder.
		std::optional<std::size_t> Uncommitted;
	};

	struct WorkFacts
	{
		WorkFactsRequest Answers;
		std::vector<WorktreeFacts> Worktrees;
		std::vector<BranchFacts> Branches; // in the request's order
	};

	// WARNING: Every method is called on a worker thread.
	class WorkPort
	{
	public:
		virtual ~WorkPort() = default;
		virtual Core::Result<WorkFacts> ReadWorkFacts(const WorkFactsRequest& request) = 0;
		virtual Core::Result<std::size_t> CountUncommitted(const std::filesystem::path& worktree) = 0;
	};
}
