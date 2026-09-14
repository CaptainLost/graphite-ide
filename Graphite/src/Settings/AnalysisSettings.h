#pragma once

#include <cstddef>

namespace Graphite {

	// One owner of the two persisted background-analysis settings. The section edits it and
	// GraphiteLayer reads it; without this they would each hold their own copy and drift apart the
	// first time one of them forgot to reload. The same reason SearchSettings exists next door.
	class AnalysisSettings
	{
	public:
		static constexpr std::size_t kMinimumBudgetMegabytes = 16;
		static constexpr std::size_t kMaximumBudgetMegabytes = 4096;
		static constexpr std::size_t kDefaultBudgetMegabytes = 128;

		void Load();

		bool IsEnabled() const { return m_Enabled; }

		std::size_t GetBudgetMegabytes() const { return m_BudgetMegabytes; }

		std::size_t GetBudgetBytes() const { return m_BudgetMegabytes * 1024ull * 1024ull; }

		void SetEnabled(bool enabled);

		void SetBudgetMegabytes(std::size_t megabytes);

	private:
		bool m_Enabled = true;
		std::size_t m_BudgetMegabytes = kDefaultBudgetMegabytes;
	};
}
