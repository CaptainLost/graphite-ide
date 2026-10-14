#pragma once

#include "Core/Git/Commit.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Requests.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct ForkBase
	{
		Core::LogRequest OwnCommits;

		// The base as the user knows it: "main", "origin/main".
		std::string BaseName;
	};

	// The base is the one a review of the branch would take, so the history and the review agree on
	// where a branch begins. Without a selected ref the checked-out branch is the one shown.
	std::optional<ForkBase> FindForkBase(const std::optional<std::string>& selectedRef,
										 const std::vector<Core::Branch>& branches);

	struct ForkSeparator
	{
		// Index in the page of the first commit that is not the branch's own.
		std::size_t BeforeCommit = 0;

		std::string Text;
	};

	// WARNING: `ownCommits` must be the branch's log without what its base already holds. A page that
	// ends before the first foreign commit, or an own list cut short, gives no separator: it would
	// stand in the wrong place.
	std::optional<ForkSeparator> FindForkSeparator(const Core::LogPage& page, const Core::LogPage& ownCommits,
												   const std::string& baseName);
}
