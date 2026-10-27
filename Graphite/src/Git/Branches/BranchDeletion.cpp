#include "Git/Branches/BranchDeletion.h"

#include <utility>

namespace Graphite {

	void BranchDeletion::Begin(std::string branch)
	{
		m_Branch = std::move(branch);
		m_CheckRunning = true;
		m_Unmerged.reset();
		m_HeldForPanel = false;
	}

	DeleteCheckLanding BranchDeletion::Land(const DeleteCheck& check)
	{
		const bool isAwaitedCheck = m_CheckRunning && m_Branch == check.Branch;

		if (!isAwaitedCheck)
		{
			return DeleteCheckLanding::Ignored;
		}

		m_CheckRunning = false;

		if (check.Failure.has_value())
		{
			Finish();
			return DeleteCheckLanding::Failed;
		}

		m_Unmerged = check.Unmerged;
		return DeleteCheckLanding::Counted;
	}

	void BranchDeletion::Finish()
	{
		m_Branch.reset();
		m_CheckRunning = false;
		m_Unmerged.reset();
		m_HeldForPanel = false;
	}

	void BranchDeletion::HoldForPanel()
	{
		if (m_Branch.has_value())
		{
			m_HeldForPanel = true;
		}
	}

	void BranchDeletion::ForgetUnlessHeld()
	{
		if (!m_HeldForPanel)
		{
			Finish();
		}
	}

	std::optional<std::string> BranchDeletion::TakeHeldOnceOnScreen(bool panelOnScreen)
	{
		if (!panelOnScreen || !m_HeldForPanel)
		{
			return std::nullopt;
		}

		m_HeldForPanel = false;
		return m_Branch;
	}

	std::optional<std::size_t> BranchDeletion::UnmergedOf(const std::string& branch) const
	{
		if (m_Branch != branch)
		{
			return std::nullopt;
		}

		return m_Unmerged;
	}
}
