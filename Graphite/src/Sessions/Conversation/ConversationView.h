#pragma once

#include "Sessions/LocalClock.h"
#include "Sessions/Markdown.h"
#include "Sessions/Pricing.h"
#include "Sessions/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SubagentLinks.h"
#include "Sessions/ToolFacts.h"
#include "Sessions/TurnParts.h"
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

		// The session's requests, which the table under a usage line lists. Without it a usage
		// line opens to nothing.
		const RequestTimeline* Timeline = nullptr;

		// Shows a request in the Requests tab, by its position in Timeline->GetRequests().
		// Without it the table's rows have no button to go there.
		std::function<void(std::size_t)> OpenRequest;

		// The subagents this transcript started, by the id of the call that started each, and a
		// way to show one's conversation. An Agent call offers that only when both are given and
		// it is among Links.
		const SubagentLinks* Links = nullptr;
		std::function<void(const std::string& toolUseId)> OpenAgent;
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
			Step,

			// An open run's usage line, after its last step: the requests cover every step of
			// the run, so under the header they would read as the steps' parent. A closed run
			// draws the line as part of its header row instead.
			RunUsage
		};

		struct Row
		{
			RowKind Kind = RowKind::User;
			std::size_t Turn = 0;

			// Part, Step and RunUsage: the index in the turn's parts. Gap: the part it comes
			// before, or kBeforeUser when it comes before the turn's user block.
			std::size_t Part = 0;

			// Step: the index in Turn::Items. Otherwise unused.
			std::size_t Item = 0;

			// Gap: the pause.
			std::chrono::milliseconds Gap{0};

			// Part: whether it is a run of steps that was open when the rows were built. The
			// header is a line shorter open than closed, so a height measured in one state must
			// not be kept for the other.
			bool Open = false;
		};

		static constexpr std::size_t kBeforeUser = SIZE_MAX;

		void Rebuild(const Session& session, float width);
		float Estimate(const Session& session, const Row& row, float width) const;
		void DrawRow(const Session& session, std::size_t index, const ConversationContext& context,
					 Core::EventBus& events);

		// The time beside a User or Claude heading, dated because a session can run over several
		// days; the full date with its year and weekday on hover.
		void DrawHeaderTime(const std::string& at, const ConversationContext& context);

		// A message from the user: the prompt that opened a turn, or one queued within it.
		void DrawUser(const std::string& text, const std::string& at, const ConversationContext& context);
		void DrawReply(const Turn& turn, const TurnPart& part, std::size_t row, const ConversationContext& context);
		void DrawStepsHeader(const Session& session, const Turn& turn, const TurnPart& part, std::size_t row,
							 const ConversationContext& context);
		void DrawRunUsage(const TurnPart& part, std::size_t row, const ConversationContext& context);
		void DrawStep(const Session& session, const TurnItem& item, std::size_t row, const ConversationContext& context,
					  Core::EventBus& events);
		void DrawInjected(const TurnItem& item, std::size_t row, const ConversationContext& context);
		// A rule across the width with the compaction's label centred on it; a click opens the
		// summary below it when there is one.
		void DrawCompaction(const TurnItem& item, std::size_t row, const ConversationContext& context);
		// A part's usage line, which opens to a line per request it covers, each with a button
		// that shows the request in the Requests tab. `width` is the room the line may take.
		void DrawUsage(const TurnPart& part, std::size_t row, float width, const ConversationContext& context);
		void DrawGap(std::chrono::milliseconds gap, const ConversationContext& context);
		void DrawMarkdown(const std::string& text, std::pair<std::size_t, std::size_t> key,
						  const ConversationContext& context);

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

		// Usage lines opened to their requests, by (turn, part), as runs are.
		std::set<std::pair<std::size_t, std::size_t>> m_OpenUsage;

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

		// A reply's markdown, parsed once and laid out once per width rather than every frame.
		struct MarkdownCache
		{
			// What was parsed, so a reply whose text changed is parsed again. A flag rather
			// than an empty text, since an empty reply is parsed too.
			bool Parsed = false;
			std::string Text;
			std::vector<MarkdownBlock> Blocks;

			// Each block's words as placed, valid for the width and font size they were placed
			// at. Empty for a code block, which is not laid out. No width until the first layout:
			// a sentinel inside the width's range would be mistaken for a real one.
			std::vector<std::vector<PlacedWord>> Layouts;
			std::optional<float> LaidWidth;
			float LaidFontSize = 0.0f;
		};

		// By (turn, part). Cleared by every rebuild.
		std::map<std::pair<std::size_t, std::size_t>, MarkdownCache> m_Markdown;

		// Each user message's and reply's time, on the reader's clock.
		LocalClock m_Clock;
	};
}
