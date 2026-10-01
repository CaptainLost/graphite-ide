#pragma once

#include "Sessions/Pricing.h"

namespace Graphite {

	// What one API request has counted so far. Claude Code writes one record per content block
	// of a response and puts the response's usage on each, and the records do not always agree:
	// the first one with usage is adopted, and every later one is merged in by MergeResponseUsage.
	// Sessions, summaries and the request timeline each keep requests in their own storage and
	// feed each request's records through one of these, so they share the rule.
	class RequestUsageAccumulator
	{
	public:
		RequestUsageAccumulator() = default;

		// A request that already counted `counts`.
		explicit RequestUsageAccumulator(const TokenCounts& counts) : m_Counts(counts), m_HasUsage(true) {}

		// Folds one record's usage in. When `total` is given it is what this request has been
		// added to, and it ends up holding the new counts in place of the old. Returns whether the
		// counts changed; the first usage always does.
		bool Add(const TokenCounts& usage, TokenCounts* total = nullptr);

		bool HasUsage() const { return m_HasUsage; }

		const TokenCounts& Counts() const { return m_Counts; }

	private:
		TokenCounts m_Counts;
		bool m_HasUsage = false;
	};

} // namespace Graphite
