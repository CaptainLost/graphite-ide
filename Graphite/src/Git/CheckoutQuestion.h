#pragma once

#include "Core/Git/Checkout.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Status.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	struct CheckoutQuestionRow
	{
		std::filesystem::path Path;

		bool Unsaved = false;

		bool Blocks = false;
	};

	struct CheckoutQuestion
	{
		std::string Title;

		std::vector<CheckoutQuestionRow> Changed;

		std::vector<CheckoutQuestionRow> Untracked;

		bool UntrackedForced = false;
	};

	std::string CheckoutRowTag(const CheckoutQuestionRow& row);

	bool NeedsCheckoutDialog(const Core::CheckoutPlan& plan, const std::vector<std::filesystem::path>& unsaved);

	CheckoutQuestion BuildCheckoutQuestion(const std::string& targetLabel, const Core::CheckoutPlan& plan,
										   const Core::WorkingTreeStatus& status,
										   const std::vector<std::filesystem::path>& unsaved);

	std::vector<std::filesystem::path> SilentReloads(const std::vector<std::filesystem::path>& changed,
													 const std::vector<std::filesystem::path>& unsavedNow);

	std::optional<std::string> BranchNameProblem(const std::string& name, const std::vector<Core::Branch>& branches);

	bool AsksBeforeDetaching(const Core::CheckoutTarget& target);

	std::string DetachedCheckoutBody(const std::string& commitLabel);

	std::string CheckoutTargetLabel(const Core::CheckoutTarget& target);
}
