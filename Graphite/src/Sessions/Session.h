#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/TranscriptRecord.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Graphite {

	struct ToolCall
	{
		std::string Id;
		std::string Name;
		std::string InputJson;
		std::string Result;
		std::string ResultJson;
		std::size_t ResultBytes = 0;
		std::string StartedAt;
		std::string FinishedAt;
		bool HasResult = false;
		bool IsError = false;
	};

	enum class ItemKind
	{
		Text,
		Thinking,
		Tool,
		Injected,
		Other
	};

	struct TurnItem
	{
		ItemKind Kind = ItemKind::Other;

		// The text or thinking, or — for Other — the block's type name.
		std::string Text;

		// For Tool: the call's index in Session::GetTools().
		std::size_t Tool = 0;

		// For Injected: one line saying what it was. Text keeps the whole of it.
		std::string Label;

		// When the record holding this item was written.
		std::string At;

		// The request that produced this item: requestId, else the record's uuid. Empty for
		// items on the user's side.
		std::string RequestId;
	};

	// What one API request cost, kept per turn so a view can show it beside the reply or the
	// steps it produced.
	struct RequestUsage
	{
		std::string Model;
		bool Fast = false;
		TokenCounts Usage;
	};

	// A prompt the user typed and everything up to the next one.
	struct Turn
	{
		std::string Prompt;
		std::string StartedAt;
		std::vector<TurnItem> Items;
		UsageByModel Usage;
		std::map<std::string, RequestUsage> Requests;
	};

	// A transcript assembled for reading. Built by appending, so a live session grows by the
	// records read since last time instead of being rebuilt.
	class Session
	{
	public:
		void Append(const std::vector<TranscriptRecord>& records);

		// Clears turns, tools and everything derived from them, and moves to a new generation.
		// Used when a transcript is read again from nothing (it shrank), so a view can tell
		// this session is not the one it was reading — not just an older version of it.
		void Reset();

		const std::vector<Turn>& GetTurns() const { return m_Turns; }
		const std::vector<ToolCall>& GetTools() const { return m_Tools; }

		// Moves whenever an append changed anything, so a view can tell its layout is stale.
		std::uint64_t GetVersion() const { return m_Version; }

		// Moves only on Reset. A view compares this, not GetVersion(), to tell a restart from
		// ordinary growth: GetVersion() is not monotonic across a Reset, so a view that rebuilt
		// against an older, higher version would wrongly treat a fresh session as one it has
		// already built rows for.
		std::uint64_t GetGeneration() const { return m_Generation; }

	private:
		Turn& CurrentTurn();
		void AddAssistant(const TranscriptRecord& record);
		void AddUser(const TranscriptRecord& record);

		std::vector<Turn> m_Turns;
		std::vector<ToolCall> m_Tools;
		std::unordered_map<std::string, std::size_t> m_ToolById;
		std::unordered_set<std::string> m_SeenRequests;
		std::uint64_t m_Version = 0;
		std::uint64_t m_Generation = 0;
	};
}
