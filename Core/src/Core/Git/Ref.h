#pragma once

#include "Core/Git/Commit.h"
#include "Core/Git/Oid.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Core {

	inline constexpr std::string_view kLocalBranchPrefix = "refs/heads/";
	inline constexpr std::string_view kRemoteBranchPrefix = "refs/remotes/";

	// "refs/heads/main" for "main".
	inline std::string LocalBranchReference(std::string_view branchName)
	{
		return std::string{kLocalBranchPrefix} + std::string{branchName};
	}

	// "main" for "refs/heads/main" and "origin/main" for "refs/remotes/origin/main"; any other name comes back whole.
	inline std::string ShortBranchName(std::string_view fullName)
	{
		for (const std::string_view prefix : {kLocalBranchPrefix, kRemoteBranchPrefix})
		{
			if (fullName.starts_with(prefix))
			{
				return std::string{fullName.substr(prefix.size())};
			}
		}

		return std::string{fullName};
	}

	enum class RefKind
	{
		Local,
		Remote
	};

	struct Branch
	{
		// "main", where FullName is "refs/heads/main".
		std::string Name;
		std::string FullName;

		RefKind Kind = RefKind::Local;

		Oid Tip;

		bool IsHead = false;

		// The remote branch its remote's HEAD names: origin/main while refs/remotes/origin/HEAD
		// points at it. Only a clone writes that HEAD, so a repository can have no such branch.
		bool IsRemoteDefault = false;

		std::optional<std::string> Upstream;

		// The upstream the branch's config names when that remote branch does not exist any more
		// ("origin/x"); Upstream is unset then.
		std::optional<std::string> MissingUpstream;

		// Commits this branch has that its upstream lacks, and the reverse. Zero and zero
		// also means "in sync", so read them only when DistanceKnown is true.
		std::size_t Ahead = 0;
		std::size_t Behind = 0;

		// False with no upstream, or when the distance could not be measured: a symbolic
		// upstream, or a graph walk that failed.
		bool DistanceKnown = false;

		// Empty when the tip commit cannot be read: past a shallow clone's boundary, or a missing
		// object.
		std::optional<CommitSummary> TipCommit;

		friend bool operator==(const Branch&, const Branch&) = default;
	};
}
