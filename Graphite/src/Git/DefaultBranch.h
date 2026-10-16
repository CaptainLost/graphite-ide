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

	// The default branch, or without one the checked-out branch. Null with neither.
	const Core::Branch* TrunkBranch(const std::vector<Core::Branch>& branches);

	// The trunk branch's tip and its upstream's. Empty without a trunk branch.
	std::vector<Core::Oid> TrunkTips(const std::vector<Core::Branch>& branches);
}
