#pragma once

#include "Sessions/Pricing.h"

namespace Graphite {

	class RequestUsageAccumulator
	{
	public:
		RequestUsageAccumulator() = default;

		explicit RequestUsageAccumulator(const TokenCounts& counts) : m_Counts(counts), m_HasUsage(true) {}

		bool Add(const TokenCounts& usage, TokenCounts* runningTotal = nullptr);

		bool HasUsage() const { return m_HasUsage; }

		const TokenCounts& GetCounts() const { return m_Counts; }

	private:
		TokenCounts m_Counts;
		bool m_HasUsage = false;
	};

} // namespace Graphite
