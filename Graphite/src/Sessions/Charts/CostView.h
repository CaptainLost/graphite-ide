#pragma once

#include "Sessions/Charts/ChartAxes.h"
#include "Sessions/LocalClock.h"
#include "Sessions/Pricing.h"
#include "Sessions/Charts/RequestChart.h"
#include "Sessions/Requests/RequestTimeline.h"
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

	struct CostBar
	{
		std::string Heading;
		bool Known = true;
	};

	class CostView
	{
	public:
		std::optional<std::size_t> Draw(const Session& session, const std::optional<SessionSummary>& summary,
										const RequestTimeline& timeline, const Pricing& pricing,
										std::optional<std::size_t> selectedRequest, const SessionPalette& palette);

	private:
		enum class CostBy
		{
			Turn,
			Request
		};

		void PriceRequests(const RequestTimeline& timeline, const Pricing& pricing);
		void FitTo(std::size_t count);

		std::optional<std::size_t> DrawTurnDetails(const Session& session, const std::vector<TurnCost>& turns,
												   const RequestTimeline& timeline, const Pricing& pricing,
												   const SessionPalette& palette);

		CostBy m_By = CostBy::Turn;

		std::optional<std::size_t> m_SelectedTurn;
		float m_ListHeight = 0.0f;

		FollowingAxis m_XAxis;
		std::size_t m_ChartCount = 0;
		std::uint64_t m_BuiltGeneration = UINT64_MAX;

		std::vector<CostLayers> m_RequestLayers;
		std::vector<double> m_RequestTotal;
		std::vector<CostBar> m_RequestBars;
		std::uint64_t m_BuiltVersion = UINT64_MAX;
		std::map<std::string, Price> m_PricedWith;

		LocalClock m_Clock;
	};
}
