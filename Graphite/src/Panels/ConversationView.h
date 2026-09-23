#pragma once

#include "Sessions/Markdown.h"
#include "Sessions/Pricing.h"
#include "Sessions/Session.h"
#include "Sessions/ToolFacts.h"
#include "Sessions/TurnParts.h"
#include "Theme/SessionPalette.h"

#include "Core/Events/EventBus.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	struct ConversationContext
	{
		const Pricing& Prices;
		const SubagentUsage& Subagents;
		const SessionPalette& Palette;

		// Moves on whenever a newer Subagents map arrives, so facts drawn from the old one
		// are worked out again.
		std::uint64_t SubagentsVersion = 0;
	};

	// A session drawn for reading: a block per user message, Claude's replies with their
	// markdown rendered, work folded into one coloured row per run of steps, injected text as
	// one quiet line, and a gap row where the session stood still. Virtualised: only rows near
	// the visible range are drawn, and row heights start as estimates and are replaced by what
	// each row really took the first time it is drawn.
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
			Step
		};

		struct Row
		{
			RowKind Kind = RowKind::User;
			std::size_t Turn = 0;

			// Part and Step: the index in the turn's parts. Gap: the part it comes before, or
			// kBeforeUser when it comes before the turn's user block.
			std::size_t Part = 0;

			// Step: the index in Turn::Items. Otherwise unused.
			std::size_t Item = 0;

			// Gap: the pause.
			std::chrono::milliseconds Gap{0};
		};

		static constexpr std::size_t kBeforeUser = SIZE_MAX;

		void Rebuild(const Session& session, float width);
		float Estimate(const Session& session, const Row& row, float width) const;
		void DrawRow(const Session& session, std::size_t index, const ConversationContext& context,
					 Core::EventBus& events);

		// A message from the user: the prompt that opened a turn, or one queued within it.
		void DrawUser(const std::string& text, const std::string& at, const ConversationContext& context);
		void DrawReply(const Turn& turn, const TurnPart& part, const ConversationContext& context);
		void DrawStepsHeader(const Session& session, const Turn& turn, const TurnPart& part, std::size_t row,
							 const ConversationContext& context);
		void DrawStep(const Session& session, const TurnItem& item, std::size_t row, const ConversationContext& context,
					  Core::EventBus& events);
		void DrawInjected(const TurnItem& item, std::size_t row, const ConversationContext& context);
		void DrawGap(std::chrono::milliseconds gap, const ConversationContext& context);
		void DrawMarkdown(const std::string& text, const ConversationContext& context);

		// "HH:MM" in the reader's time zone, or nothing when the timestamp does not parse.
		std::string ClockTime(const std::string& timestamp);

		// The reader's offset from UTC at `instant`; zero when the time zone is unknown.
		std::chrono::minutes OffsetAt(std::chrono::sys_time<std::chrono::milliseconds> instant);

		void Toggle(std::set<std::pair<std::size_t, std::size_t>>& open, std::pair<std::size_t, std::size_t> key,
					std::size_t row);

		// The parts of every turn, kept with the rows built from them.
		std::vector<std::vector<TurnPart>> m_Parts;

		std::vector<Row> m_Rows;
		std::vector<float> m_Heights;
		std::vector<bool> m_Measured;
		std::vector<float> m_Offsets;

		// Opened step runs, by (turn, part); expanded steps and injected rows, by (turn, item).
		// Everything starts folded.
		std::set<std::pair<std::size_t, std::size_t>> m_OpenParts;
		std::set<std::pair<std::size_t, std::size_t>> m_OpenItems;

		std::uint64_t m_BuiltVersion = UINT64_MAX;

		// UINT64_MAX means "never built"; a real session's generation starts at 0, so this
		// can never collide with one and mistake a first build for the same session.
		std::uint64_t m_BuiltGeneration = UINT64_MAX;
		float m_BuiltWidth = -1.0f;
		bool m_OffsetsDirty = true;
		bool m_FollowTail = true;

		// Each tool's facts, by tool index, worked out when its row is first drawn. Cleared
		// by every rebuild, and the Agent calls' when a newer subagent map arrives.
		std::vector<std::optional<ToolFacts>> m_Facts;
		std::uint64_t m_FactsSubagentsVersion = 0;

		// The reader's time zone, looked up once and null when it could not be, and the span
		// of time the offset last asked of it holds for.
		const std::chrono::time_zone* m_Zone = nullptr;
		bool m_ZoneResolved = false;
		std::optional<std::chrono::sys_info> m_ZoneInfo;
	};
}
