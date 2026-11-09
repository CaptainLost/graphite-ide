#pragma once

#include "Git/GitModel.h"
#include "Git/GitStatusText.h"

#include "Core/Git/Checkout.h"

#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	std::string WriteRunningText(const GitModel::WriteSubject& subject);

	std::string WriteDoneText(const GitModel::WriteSubject& subject);

	struct GitPanelCheckouts
	{
		std::optional<Core::CheckoutTarget> Planning;

		std::optional<Core::CheckoutTarget> Asking;

		std::optional<Core::CheckoutTarget> HeldForPanel;

		std::optional<Core::CheckoutTarget> Writing;

		std::optional<std::string> DeleteHeldForPanel;
	};

	struct GitPanelWait
	{
		std::optional<std::string> CheckingOut;

		std::optional<std::string> AwaitingAnswer;
	};

	GitPanelWait DescribePanelWait(const GitPanelCheckouts& checkouts);

	// A fetch, pull or push: its running text while it runs, else its done text while that is shown.
	struct NetworkActivity
	{
		std::optional<std::string> Running;
		std::optional<std::string> Done;
	};

	// A question waiting for its answer comes first, then a running write, then a running network operation, then the
	// write's done text and the network operation's.
	// WARNING: Says nothing of a running checkout, which the branch item names in place of the head.
	std::optional<GitActivity> DescribeGitActivity(const std::optional<GitModel::WriteReport>& report,
												   const std::optional<std::string>& awaitingAnswer,
												   const NetworkActivity& network = {});

	// A write into another worktree, said as the open folder's writes are, after the folder's name:
	// "eager-scribbling-frog: Staging a.txt…".
	std::optional<GitActivity> DescribeWorktreeWrite(const std::optional<GitModel::WriteReport>& report, std::string_view folder);

	// The open folder's activity while it runs or waits for an answer; else a worktree's write while it runs; else the open
	// folder's done text, and the worktree's when the open folder has nothing to say.
	std::optional<GitActivity> WithWorktreeWrite(const std::optional<GitActivity>& openFolder,
												 const std::optional<GitActivity>& worktree);
}
