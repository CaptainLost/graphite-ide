#pragma once

#include "Core/Git/Ref.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// "refs/heads/main" or "refs/remotes/origin/main": the full name tells a local branch from a remote one.
	const Core::Branch* BranchWithFullName(const std::vector<Core::Branch>& branches, std::string_view fullName);

	const Core::Branch* RemoteBranchNamed(const std::vector<Core::Branch>& branches, const std::string& name);

	const Core::Branch* CheckedOutBranch(const std::vector<Core::Branch>& branches);

	// The upstream a log walk of `ref` measures its commits to push against, "origin/main": that of the branch with the
	// full name, or of the checked-out branch for none. Empty when the branch is not listed or has no upstream.
	std::string UpstreamOfWalk(const std::vector<Core::Branch>& branches, const std::optional<std::string>& ref);
}
