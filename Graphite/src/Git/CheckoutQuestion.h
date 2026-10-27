#pragma once

#include "Core/Git/Checkout.h"
#include "Core/Git/Ref.h"
#include "Core/Git/Status.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
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

	struct LandedCheckoutPlan
	{
		bool Failed = false;
		bool NeedsDialog = false;
		bool PanelOnScreen = false;
	};

	enum class CheckoutPlanVerdict
	{
		ReportFailure,
		CheckOut,
		Ask,
		HoldForPanel
	};

	// WARNING: Decided once, as the plan lands. A plan judged again later, against the buffers of that moment,
	// checks out a branch nobody confirmed once the reader has saved the edits its question was about.
	CheckoutPlanVerdict DecideOnCheckoutPlan(const LandedCheckoutPlan& plan);

	class HeldCheckout
	{
	public:
		void Hold(Core::CheckoutTarget target) { m_Target = std::move(target); }

		void Forget() { m_Target.reset(); }

		const std::optional<Core::CheckoutTarget>& HeldTarget() const { return m_Target; }

		// WARNING: Hands out the target only, never the old plan: the checkout is planned again before it is asked about.
		std::optional<Core::CheckoutTarget> TakeOnceOnScreen(bool panelOnScreen);

	private:
		std::optional<Core::CheckoutTarget> m_Target;
	};
}
