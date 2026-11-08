#pragma once

#include "Core/Git/Command/GitNetwork.h"
#include "Git/GitActivity.h"
#include "Git/Network/NetworkModel.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Graphite {

	// "Fetching… 45%", "Fetching full history… 45%", "Pulling…", "Pushing…"; empty while nothing runs.
	std::string RunningText(const NetworkState& state);

	// "Fetched", "Pulled 5 commits", "Pushed 2 commits", "Already up to date", "Nothing to push", "Fetch cancelled".
	std::string DoneText(const NetworkDone& done);

	// "Fetch failed", "Pull failed", "Push failed".
	std::string FailureTitle(NetworkOperation operation);

	// The sentences said when an operation cannot run, or stops before git does.
	std::string DivergedText(const std::string& branch, const std::string& upstream, std::size_t ahead, std::size_t behind);
	std::string NoLongerCheckedOutText(const std::string& branch, const std::string& upstream);
	std::string FolderChangedText(const std::string& upstream);
	std::string NoUpstreamText(const std::string& branch);
	std::string LocalUpstreamText(const std::string& branch, const std::string& upstream);
	std::string NoRemoteText();
	std::string NoBranchText();

	// What Push says when it publishes the branch under its own name: "Push my-fix to origin as origin/my-fix", and, when
	// the branch has an upstream of another name, "; it then tracks origin/my-fix instead of origin/main".
	std::string PublishText(const std::string& branch, const std::string& remote, const std::optional<std::string>& upstream);

	// A failure's message: the hint's sentence naming the remote and the branch when they are known, in place of Core's,
	// which names neither; for Pull's local changes, the files git listed anywhere in its errors, ten at most.
	std::string FailureMessage(NetworkOperation operation, const Core::NetworkFailure& failure, const NetworkNames& names);

	NetworkActivity DescribeNetworkActivity(const NetworkState& state, const std::optional<NetworkDone>& shown);
}
