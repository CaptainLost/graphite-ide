#pragma once

#include "Panels/ConversationView.h"
#include "Panels/LocalClock.h"
#include "Panels/RequestChartView.h"
#include "Panels/SentTextView.h"
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

	// Every API request of a session: a chart of them above — see RequestChartView — a table of them
	// the reader sorts by any column, and — once a request is picked — under it, split by a
	// divider the reader drags and closed from its corner, what that request sent and returned.
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

		// Each request's cost, for its cell and the chart, and the prices they were worked out
		// with: an edited price moves both without the timeline changing.
		std::vector<CostResult> m_Costs;
		std::map<std::string, Price> m_PricedWith;

		// The first position whose cost is not worked out yet.
		std::size_t m_PriceFrom = 0;

		// The positions the filters let through, in the table's order.
		std::vector<std::size_t> m_Shown;

		// By position in GetRequests(): whether the filters let the request through, for the
		// chart to dim the ones they leave out.
		std::vector<bool> m_Passes;

		RequestChartView m_Chart;

		RequestColumn m_SortColumn = RequestColumn::Index;
		bool m_SortAscending = true;

		std::optional<std::size_t> m_Selected;
		bool m_RebuiltOnly = false;
		std::string m_ToolFilter;

		std::optional<std::size_t> m_PendingReveal;
		std::uint64_t m_RevealGeneration = 0;

		// The table's height above the details, as DrawSplitResults keeps it: zero until the
		// first frame that shows both, and kept across the pane being closed and opened again.
		float m_ListHeight = 0.0f;

		// The item of the selected request that the list and the text beside it both show
		// selected, by its position in Sent then Response; cleared when another request is
		// selected, since that position then names some other item or none.
		std::optional<std::size_t> m_SelectedItem;

		// The item the list scrolls to on the next draw: one clicked in the text.
		std::optional<std::size_t> m_RevealItem;

		// The request the details last drew — its Key, and its Index for a request with no key —
		// and how many items it had sent, to notice another request, or items put ahead of the
		// selected one. Index 0 is no request: a request's Index starts at 1.
		std::string m_DetailsKey;
		std::size_t m_DetailsIndex = 0;
		std::size_t m_DetailsSent = 0;

		// The list's width left of the handle; below zero until a draw with enough room picks
		// one — see DrawDetails, which waits for twice the split's minimum before storing it.
		float m_ItemsWidth = -1.0f;

		SentTextView m_SentText;

		std::uint64_t m_BuiltVersion = UINT64_MAX;

		// UINT64_MAX means "never built", as in the conversation: a real generation starts at 0.
		std::uint64_t m_BuiltGeneration = UINT64_MAX;
		bool m_FilterDirty = true;

		LocalClock m_Clock;
	};
}
