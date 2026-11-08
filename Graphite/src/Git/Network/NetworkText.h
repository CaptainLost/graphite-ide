#pragma once

#include "Core/Git/Command/GitNetwork.h"
#include "Git/GitActivity.h"
#include "Git/Network/NetworkModel.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Graphite {

	// "Fetching… 45%", "Pulling…", "Pushing…"; empty while nothing runs.
	std::string RunningText(const NetworkState& state);

	// "Fetched", "Pulled 5 commits", "Pushed 2 commits", "Fetch cancelled".
	std::string DoneText(const NetworkDone& done);

	// "Fetch failed", "Pull failed", "Push failed".
	std::string FailureTitle(NetworkOperation operation);

	// The sentences said when an operation cannot run, or stops before git does.
	std::string DivergedText(const std::string& branch, const std::string& upstream, std::size_t ahead, std::size_t behind);
	std::string NoLongerCheckedOutText(const std::string& branch, const std::string& upstream);
	std::string NoUpstreamText(const std::string& branch);
	std::string NoRemoteText();
	std::string NoBranchText();

	// A failure's message: the hint's sentence naming the remote and the branch when they are known, in place of Core's,
	// which names neither; for Pull's local changes, the files git listed anywhere in its errors, ten at most.
	std::string FailureMessage(NetworkOperation operation, const Core::NetworkFailure& failure, const NetworkNames& names);

	NetworkActivity DescribeNetworkActivity(const NetworkState& state, const std::optional<NetworkDone>& shown);
}
