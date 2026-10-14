#include "Sessions/Requests/RequestUsageAccumulator.h"

namespace Graphite {

	bool RequestUsageAccumulator::Add(const TokenCounts& usage, TokenCounts* runningTotal)
	{
		const TokenCounts merged = m_HasUsage ? MergeResponseUsage(m_Counts, usage) : usage;
		const bool changed = !m_HasUsage || merged != m_Counts;

		if (runningTotal != nullptr)
		{
			if (m_HasUsage)
			{
				*runningTotal -= m_Counts;
			}

			*runningTotal += merged;
		}

		m_Counts = merged;
		m_HasUsage = true;
		return changed;
	}

} // namespace Graphite
