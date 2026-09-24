#pragma once

#include "Panels/ChartAxes.h"
#include "Panels/LocalClock.h"
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

	// What one bar of the Cost chart stands for, for its tooltip: a heading naming it, and
	// whether its buckets are the whole of its cost — false for one that used a model with no
	// price, whose numbers then read "?".
	struct CostBar
	{
		std::string Heading;
		bool Known = true;
	};

	// A session's Cost tab: its totals by model, a chart of what each turn or each request
	// cost, stacked by bucket under the session's running total, and a table of the turns.
	class CostView
	{
	public:
		// Returns the position in `timeline` of a request clicked on the chart, for the
		// Requests tab to show. `selected` is the request that tab has picked, banded on the
		// chart when it charts requests.
		std::optional<std::size_t> Draw(const Session& session, const std::optional<SessionSummary>& summary,
										 const RequestTimeline& timeline, const Pricing& pricing,
										 std::optional<std::size_t> selected, const SessionPalette& palette);

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
		std::vector<CostBar> m_RequestBars;
		std::uint64_t m_BuiltVersion = UINT64_MAX;
		std::map<std::string, Price> m_PricedWith;

		LocalClock m_Clock;
	};
}
