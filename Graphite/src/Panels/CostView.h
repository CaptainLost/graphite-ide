#pragma once

#include "Panels/ChartAxes.h"
#include "Sessions/Pricing.h"
#include "Sessions/RequestChart.h"
#include "Sessions/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Theme/SessionPalette.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// A session's Cost tab: its totals by model, a chart of what each turn or each request
	// cost, stacked by bucket under the session's running total, and a table of the turns.
	class CostView
	{
	public:
		// Returns the position in `timeline` of a request clicked on the chart, for the
		// Requests tab to show.
		std::optional<std::size_t> Draw(const Session& session, const std::optional<SessionSummary>& summary,
										 const RequestTimeline& timeline, const Pricing& pricing,
										 const SessionPalette& palette);

	private:
		// What one bar of the chart stands for.
		enum class CostBy
		{
			Turn,
			Request
		};

		void PriceRequests(const RequestTimeline& timeline, const Pricing& pricing);
		void FitTo(std::size_t count);

		CostBy m_By = CostBy::Turn;

		// The turns or requests axis, fitted to them as they arrive until the reader moves it;
		// and how many bars it was last fitted to.
		FollowingAxis m_XAxis;
		std::size_t m_ChartCount = 0;
		std::uint64_t m_BuiltGeneration = UINT64_MAX;

		// Each request's cost by layer and the session's cost up to it, worked out again when
		// the timeline or the prices change.
		std::vector<CostLayers> m_RequestLayers;
		std::vector<double> m_RequestTotal;
		std::uint64_t m_BuiltVersion = UINT64_MAX;
		std::map<std::string, Price> m_PricedWith;
	};
}
