#pragma once

#include "Panels/ChartAxes.h"
#include "Panels/LocalClock.h"
#include "Sessions/Pricing.h"
#include "Sessions/RequestChart.h"
#include "Sessions/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
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

		// The pane under the turns for the turn picked: where its money went, the tools it
		// called, and its dearest requests. Returns the position in `timeline` of a request
		// clicked there, for the Requests tab to show.
		std::optional<std::size_t> DrawTurnDetails(const Session& session, const std::vector<TurnCost>& turns,
												   const RequestTimeline& timeline, const Pricing& pricing,
												   const SessionPalette& palette);

		CostBy m_By = CostBy::Turn;

		// The turn whose row or bar was clicked, shown in the pane below the turns until the
		// pane is closed; and the height the reader dragged the turns to above it.
		std::optional<std::size_t> m_SelectedTurn;
		float m_ListHeight = 0.0f;

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
