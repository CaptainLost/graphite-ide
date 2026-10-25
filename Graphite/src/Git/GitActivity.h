#pragma once

#include "Git/GitModel.h"
#include "Git/GitStatusText.h"

#include "Core/Git/Checkout.h"
#include "Core/Git/Ref.h"

#include <optional>
#include <string>

namespace Graphite {

	// "Staging 2,000 files…", for a write from its click until the status read after it lands.
	std::string WriteRunningText(const GitModel::WriteSubject& subject);

	// "Staged 2,000 files", for a few seconds after that.
	std::string WriteDoneText(const GitModel::WriteSubject& subject);

	// What the Git panel has started for the reader and not yet handed on to the model as a write.
	struct GitPanelCheckouts
	{
		// A plan asked for whose answer has not been taken.
		std::optional<Core::CheckoutTarget> Planning;

		// A plan whose question is asked, on screen or queued behind another dialog.
		std::optional<Core::CheckoutTarget> Asking;

		// A plan whose question waits for the Git panel to be on screen.
		std::optional<Core::CheckoutTarget> HeldForPanel;

		// The checkout the model is writing.
		std::optional<Core::CheckoutTarget> Writing;

		// The branch whose delete question waits for the Git panel to be on screen.
		std::optional<std::string> DeleteHeldForPanel;
	};

	struct GitPanelWait
	{
		// Named in the status bar in place of the head it leaves, from the click until the head after it lands.
		std::optional<std::string> CheckingOut;

		// The action whose question waits for the Git panel: "Checkout of feature/x".
		std::optional<std::string> AwaitingAnswer;
	};

	GitPanelWait DescribePanelWait(const GitPanelCheckouts& checkouts);

	// The checkout whose target branch row turns a loader where the head's mark sits.
	std::optional<Core::CheckoutTarget> CheckoutMarkedOnRows(const GitPanelCheckouts& checkouts);

	bool IsCheckoutTargetOf(const Core::CheckoutTarget& target, const Core::Branch& branch);

	// What the status bar says beside the branch: a question waiting in Git first, then a write running, then one
	// just done. A checkout running is named in place of the head instead.
	std::optional<GitActivity> DescribeGitActivity(const std::optional<GitModel::WriteReport>& report,
												   const std::optional<std::string>& awaitingAnswer);
}
