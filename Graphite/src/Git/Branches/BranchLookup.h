#pragma once

#include "Core/Git/Ref.h"

#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	// "refs/heads/main" or "refs/remotes/origin/main": the full name tells a local branch from a remote one.
	const Core::Branch* BranchWithFullName(const std::vector<Core::Branch>& branches, std::string_view fullName);

	const Core::Branch* RemoteBranchNamed(const std::vector<Core::Branch>& branches, const std::string& name);

	const Core::Branch* CheckedOutBranch(const std::vector<Core::Branch>& branches);
}
