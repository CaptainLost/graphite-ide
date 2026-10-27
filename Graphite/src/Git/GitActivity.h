#pragma once

#include "Git/GitModel.h"
#include "Git/GitStatusText.h"

#include "Core/Git/Checkout.h"
#include "Core/Git/Ref.h"

#include <optional>
#include <string>

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

	std::optional<Core::CheckoutTarget> CheckoutMarkedOnRows(const GitPanelCheckouts& checkouts);

	bool IsCheckoutTargetOf(const Core::CheckoutTarget& target, const Core::Branch& branch);

	// WARNING: Says nothing of a running checkout, which the branch item names in place of the head.
	std::optional<GitActivity> DescribeGitActivity(const std::optional<GitModel::WriteReport>& report,
												   const std::optional<std::string>& awaitingAnswer);
}
