#pragma once

#include "Sessions/LocalClock.h"
#include "Sessions/Conversation/Markdown.h"
#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/Transcript/SubagentLinks.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "Sessions/Transcript/TurnParts.h"
#include "Theme/SessionPalette.h"

#include "Core/Events/EventBus.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace Graphite {

	struct ConversationContext
	{
		const Pricing& Prices;
		const SubagentUsage& Subagents;
		const SessionPalette& Palette;
		std::uint64_t SubagentsVersion = 0;
		const RequestTimeline* Timeline = nullptr;
		std::function<void(std::size_t)> OpenRequest;
		const SubagentLinks* Links = nullptr;
		std::function<void(const std::string& toolUseId)> OpenAgent;
	};

	class ConversationView
	{
	public:
		void Draw(const Session& session, const ConversationContext& context, Core::EventBus& events);

	private:
		enum class RowKind
		{
			User,
			Gap,
			Part,
			Step,
			RunUsage
		};

		struct Row
		{
			RowKind Kind = RowKind::User;
			std::size_t TurnIndex = 0;
			std::size_t PartIndex = 0;
			std::size_t ItemIndex = 0;
			std::chrono::milliseconds Pause{0};
			bool WasOpenWhenBuilt = false;
		};

		using RowIdentity = std::tuple<int, std::size_t, std::size_t, std::size_t, bool>;

		static constexpr std::size_t kBeforeUser = SIZE_MAX;
		static constexpr std::uint64_t kNeverBuilt = UINT64_MAX;

		static RowIdentity IdentityOf(const Row& row);
		static void PushGap(std::vector<Row>& rows, std::optional<std::chrono::milliseconds> pause,
							std::size_t turnIndex, std::size_t partIndex);
		static void PushGapTo(std::vector<Row>& rows, const std::string& latestTime, std::string_view nextTime,
							  std::size_t turnIndex, std::size_t partIndex);
		static void PushGapBeforeItem(std::vector<Row>& rows, const std::string& latestTime, const TurnItem& item,
									  std::size_t turnIndex, std::size_t partIndex);

		void Rebuild(const Session& session, float width);
		float Estimate(const Session& session, const Row& row, float width) const;
		void DrawRow(const Session& session, std::size_t index, const ConversationContext& context,
					 Core::EventBus& events);
		void DrawHeaderTime(const std::string& timestamp, const ConversationContext& context);
		void DrawUser(const std::string& text, const std::string& timestamp, const ConversationContext& context);
		void DrawReply(const Turn& turn, const TurnPart& part, std::size_t rowIndex,
					   const ConversationContext& context);
		void DrawStepsHeader(const Session& session, const Turn& turn, const TurnPart& part, std::size_t rowIndex,
							 const ConversationContext& context);
		void DrawRunUsage(const TurnPart& part, std::size_t rowIndex, const ConversationContext& context);
		void DrawStep(const Session& session, const TurnItem& item, std::size_t rowIndex,
					  const ConversationContext& context, Core::EventBus& events);
		void DrawInjected(const TurnItem& item, std::size_t rowIndex, const ConversationContext& context);
		void DrawCompaction(const TurnItem& item, std::size_t rowIndex, const ConversationContext& context);
		void DrawUsage(const TurnPart& part, std::size_t rowIndex, float width, const ConversationContext& context);
		void DrawGap(std::chrono::milliseconds gap, const ConversationContext& context);
		void DrawMarkdown(const std::string& text, std::pair<std::size_t, std::size_t> key,
						  const ConversationContext& context);

		void Toggle(std::set<std::pair<std::size_t, std::size_t>>& openKeys, std::pair<std::size_t, std::size_t> key,
					std::size_t rowIndex);

		std::vector<std::vector<TurnPart>> m_Parts;

		std::vector<Row> m_Rows;
		std::vector<float> m_Heights;
		std::vector<bool> m_Measured;
		std::vector<float> m_Offsets;

		std::set<std::pair<std::size_t, std::size_t>> m_OpenParts;
		std::set<std::pair<std::size_t, std::size_t>> m_OpenItems;
		std::set<std::pair<std::size_t, std::size_t>> m_OpenUsage;

		std::uint64_t m_BuiltVersion = kNeverBuilt;
		std::uint64_t m_BuiltGeneration = kNeverBuilt;
		float m_BuiltWidth = -1.0f;
		bool m_OffsetsDirty = true;
		bool m_FollowTail = true;

		std::vector<std::optional<ToolFacts>> m_Facts;
		std::uint64_t m_FactsSubagentsVersion = 0;

		struct MarkdownCache
		{
			// WARNING: Parsed is a flag rather than an empty Text, since an empty reply is parsed too.
			bool Parsed = false;
			std::string Text;
			std::vector<MarkdownBlock> Blocks;
			std::vector<std::vector<PlacedWord>> Layouts;
			std::optional<float> LaidWidth;
			float LaidFontSize = 0.0f;
		};

		std::map<std::pair<std::size_t, std::size_t>, MarkdownCache> m_Markdown;

		LocalClock m_Clock;
	};
}
