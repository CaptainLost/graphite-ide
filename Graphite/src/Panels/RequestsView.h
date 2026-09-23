#pragma once

#include "Panels/ConversationView.h"
#include "Panels/LocalClock.h"
#include "Sessions/Pricing.h"
#include "Sessions/RequestSort.h"
#include "Sessions/RequestTimeline.h"
#include "Sessions/Session.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Graphite {

	// Every API request of a session: a chart of each one's context and cost, a table of them
	// the reader sorts by any column, and under it, split by a handle the reader drags, what
	// the selected request sent and returned.
	class RequestsView
	{
	public:
		void Draw(const RequestTimeline& timeline, const Session& session, const ConversationContext& context);

		// Clears the filters, selects the request at `requestIndex` — its position in
		// GetRequests() — and scrolls the table to it on the next draw. `generation` is the
		// session generation the position was taken from: a position from an earlier reading of
		// the transcript names some other request, and is dropped.
		void Reveal(std::size_t requestIndex, std::uint64_t generation);

	private:
		void Rebuild(const RequestTimeline& timeline, const Session& session);
		void PriceRequests(const RequestTimeline& timeline, const Pricing& pricing);
		void Filter(const RequestTimeline& timeline, const Pricing& pricing);
		void DrawChart(const SessionPalette& palette);
		void DrawFilters(const RequestTimeline& timeline, const Pricing& pricing);
		void DrawTable(const RequestTimeline& timeline, const ConversationContext& context);
		void DrawTableRow(const ApiRequest& request, std::size_t position, const ConversationContext& context);
		void DrawDetails(const RequestTimeline& timeline, const ConversationContext& context);

		// By position in GetRequests(), for every request: the tools it used, and the same names
		// joined as its Tools cell reads them.
		std::vector<std::vector<std::string>> m_ToolNames;
		std::vector<std::string> m_ToolLabels;

		// Every tool any request used, for the filter.
		std::set<std::string> m_AllTools;

		// The chart's series, one value per request.
		std::vector<double> m_ChartX;
		std::vector<double> m_ChartTotal;
		std::vector<double> m_ChartCached;
		std::vector<double> m_ChartRead;
		std::vector<double> m_ChartCost;

		// Each request's cost, for its cell and the chart's line, and the prices they were
		// worked out with: an edited price moves both without the timeline changing.
		std::vector<CostResult> m_Costs;
		std::map<std::string, Price> m_PricedWith;

		// The first position whose cost is not worked out yet.
		std::size_t m_PriceFrom = 0;

		// The positions the filters let through, in the table's order.
		std::vector<std::size_t> m_Shown;

		RequestColumn m_SortColumn = RequestColumn::Index;
		bool m_SortAscending = true;

		std::optional<std::size_t> m_Selected;
		bool m_RebuiltOnly = false;
		std::string m_ToolFilter;

		std::optional<std::size_t> m_PendingReveal;
		std::uint64_t m_RevealGeneration = 0;

		// The table's height above the handle; below zero until the first draw picks one.
		float m_ListHeight = -1.0f;

		std::uint64_t m_BuiltVersion = UINT64_MAX;

		// UINT64_MAX means "never built", as in the conversation: a real generation starts at 0.
		std::uint64_t m_BuiltGeneration = UINT64_MAX;
		bool m_FilterDirty = true;

		LocalClock m_Clock;
	};
}
