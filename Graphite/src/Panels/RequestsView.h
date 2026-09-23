#pragma once

#include "Panels/ConversationView.h"
#include "Panels/LocalClock.h"
#include "Sessions/Pricing.h"
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

	// Every API request of a session: a chart of each one's context and cost, and a list that
	// folds each to a line and opens it to what it sent and returned. The list is virtualised
	// as the conversation is: rows start at an estimated height and keep what they really took
	// the first time they are drawn.
	class RequestsView
	{
	public:
		void Draw(const RequestTimeline& timeline, const Session& session, const ConversationContext& context);

		// Clears the filters, opens the request at `requestIndex` — its position in
		// GetRequests() — and scrolls the list to it on the next draw.
		void Reveal(std::size_t requestIndex);

	private:
		void Rebuild(const RequestTimeline& timeline, const Session& session);
		void PriceChart(const RequestTimeline& timeline, const Pricing& pricing);
		void Filter(const RequestTimeline& timeline);
		void DrawChart(const SessionPalette& palette);
		void DrawFilters(const RequestTimeline& timeline);
		void DrawList(const RequestTimeline& timeline, const ConversationContext& context);
		void DrawRow(const ApiRequest& request, std::size_t position, const ConversationContext& context);
		float Estimate(const ApiRequest& request, std::size_t position) const;

		// By position in GetRequests(), for every request.
		std::vector<float> m_Heights;
		std::vector<bool> m_Measured;
		std::vector<std::vector<std::string>> m_ToolNames;

		// The chart's series, one value per request.
		std::vector<double> m_ChartX;
		std::vector<double> m_ChartTotal;
		std::vector<double> m_ChartCached;
		std::vector<double> m_ChartRead;
		std::vector<double> m_ChartCost;

		// The prices m_ChartCost was worked out with: an edited price moves the line without
		// the timeline changing.
		std::map<std::string, Price> m_PricedWith;
		bool m_PriceDirty = true;

		// Every tool any request used, sorted, for the filter.
		std::vector<std::string> m_AllTools;

		// The positions the filters let through, and each one's top in the list.
		std::vector<std::size_t> m_Shown;
		std::vector<float> m_Offsets;

		std::set<std::size_t> m_Open;
		bool m_RebuiltOnly = false;
		std::string m_ToolFilter;
		std::optional<std::size_t> m_PendingReveal;

		std::uint64_t m_BuiltVersion = UINT64_MAX;

		// UINT64_MAX means "never built", as in the conversation: a real generation starts at 0.
		std::uint64_t m_BuiltGeneration = UINT64_MAX;
		bool m_FilterDirty = true;
		bool m_OffsetsDirty = true;

		LocalClock m_Clock;
	};
}
