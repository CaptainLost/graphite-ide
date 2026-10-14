#pragma once

#include "Sessions/Conversation/ConversationView.h"
#include "Sessions/LocalClock.h"
#include "Sessions/Charts/RequestChartView.h"
#include "Sessions/Requests/SentTextView.h"
#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestGroups.h"
#include "Sessions/Requests/RequestSort.h"
#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Session.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Graphite {

	class RequestsView
	{
	public:
		void Draw(const RequestTimeline& timeline, const Session& session, const ConversationContext& context,
				  Core::EventBus& events);

		void Reveal(std::size_t requestIndex, std::uint64_t generation);

		std::optional<std::size_t> GetSelected() const { return m_Selected; }

	private:
		static constexpr std::uint64_t kNeverBuilt = UINT64_MAX;

		void Rebuild(const RequestTimeline& timeline, const Session& session);
		void PriceRequests(const RequestTimeline& timeline, const Pricing& pricing);
		bool PassesFilters(const std::vector<ApiRequest>& requests, std::size_t position) const;
		void Filter(const RequestTimeline& timeline, const Pricing& pricing);
		void Regroup(const RequestTimeline& timeline, const Pricing& pricing);
		void RebuildTurns(const Session& session, const RequestTimeline& timeline);
		void DrawFilters(const RequestTimeline& timeline, const Pricing& pricing);
		void DrawTable(const RequestTimeline& timeline, const ConversationContext& context);
		void DrawTableRow(const ApiRequest& request, std::size_t position, std::size_t row,
						  const ConversationContext& context);
		void DrawTurnMark(std::size_t row, const SessionPalette& palette);
		void DrawDetails(const RequestTimeline& timeline, const ConversationContext& context, Core::EventBus& events);

		std::vector<std::vector<std::string>> m_ToolNames;
		std::vector<std::string> m_ToolLabels;

		std::set<std::string> m_AllTools;

		std::vector<CostResult> m_Costs;
		std::map<std::string, Price> m_PricedWith;

		std::size_t m_PriceFrom = 0;

		std::vector<std::size_t> m_Shown;

		std::vector<std::size_t> m_TurnOf;
		std::vector<std::string> m_TurnPrompts;
		std::uint64_t m_TurnsVersion = kNeverBuilt;
		std::uint64_t m_TurnsTimelineVersion = kNeverBuilt;

		bool m_GroupByTurn = true;
		std::vector<TurnGroup> m_Groups;
		std::vector<std::size_t> m_RowGroup;

		std::vector<bool> m_Passes;

		RequestChartView m_Chart;

		RequestColumn m_SortColumn = RequestColumn::Index;
		bool m_SortAscending = true;

		std::optional<std::size_t> m_Selected;
		bool m_RebuiltOnly = false;
		std::string m_ToolFilter;

		std::optional<std::size_t> m_PendingReveal;
		std::uint64_t m_RevealGeneration = 0;

		float m_ListHeight = 0.0f;

		std::optional<std::size_t> m_SelectedItem;

		std::optional<std::size_t> m_RevealItem;

		std::string m_DetailsKey;
		std::size_t m_DetailsIndex = 0;
		std::size_t m_DetailsSent = 0;

		float m_ItemsWidth = -1.0f;

		SentTextView m_SentText;

		std::uint64_t m_BuiltVersion = kNeverBuilt;

		std::uint64_t m_BuiltGeneration = kNeverBuilt;
		bool m_FilterDirty = true;

		LocalClock m_Clock;
	};
}
