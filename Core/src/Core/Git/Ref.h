#pragma once

#include "Core/Git/Oid.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Core {

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

		// Commits this branch has that its upstream lacks, and the reverse. Zero and zero
		// also means "in sync", so read them only when DistanceKnown is true.
		std::size_t Ahead = 0;
		std::size_t Behind = 0;

		// False with no upstream, or when the distance could not be measured: a symbolic
		// upstream, or a graph walk that failed.
		bool DistanceKnown = false;
	};
}
