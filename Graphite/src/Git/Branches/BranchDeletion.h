#pragma once

#include "Git/GitModel.h"

#include <cstddef>
#include <optional>
#include <string>

namespace Graphite {

	enum class DeleteCheckLanding
	{
		Ignored,
		Failed,
		Counted
	};

	class BranchDeletion
	{
	public:
		void Begin(std::string branch);

		DeleteCheckLanding Land(const DeleteCheck& check);

		// WARNING: Called for Delete, Cancel and Escape alike. A check still running is then dropped as it lands, or
		// its failure would toast about a question the reader already closed.
		void Finish();

		void HoldForPanel();
		void ForgetUnlessHeld();
		std::optional<std::string> TakeHeldOnceOnScreen(bool panelOnScreen);

		const std::optional<std::string>& UnansweredBranch() const { return m_Branch; }
		bool IsHeldForPanel() const { return m_HeldForPanel; }

		std::optional<std::size_t> UnmergedOf(const std::string& branch) const;

	private:
		std::optional<std::string> m_Branch;
		bool m_CheckRunning = false;
		std::optional<std::size_t> m_Unmerged;
		bool m_HeldForPanel = false;
	};
}
