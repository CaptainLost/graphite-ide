#pragma once

#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"

#include <string>
#include <vector>

namespace Graphite {

	const Core::Branch* RemoteBranchNamed(const std::vector<Core::Branch>& branches, const std::string& name);

	const Core::Branch* CheckedOutBranch(const std::vector<Core::Branch>& branches);

	// The local branch tracking the branch origin/HEAD names, the one named like it first; with
	// none tracking it, that remote branch. Null without origin/HEAD.
	const Core::Branch* DefaultBranch(const std::vector<Core::Branch>& branches);

	// The default branch's tip and its upstream's; without a default branch, the checked-out
	// branch's and its upstream's. Empty with neither.
	std::vector<Core::Oid> TrunkTips(const std::vector<Core::Branch>& branches);
}