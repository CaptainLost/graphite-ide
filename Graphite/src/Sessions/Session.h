#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/TranscriptRecord.h"

#include <cstddef>
#include <cstdint>
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
		Other
	};

	struct TurnItem
	{
		ItemKind Kind = ItemKind::Other;

		// The text or thinking, or — for Other — the block's type name.
		std::string Text;

		// For Tool: the call's index in Session::GetTools().
		std::size_t Tool = 0;
	};

	// A prompt the user typed and everything up to the next one.
	struct Turn
	{
		std::string Prompt;
		std::string StartedAt;
		std::vector<TurnItem> Items;
		UsageByModel Usage;
	};

	// A transcript assembled for reading. Built by appending, so a live session grows by the
	// records read since last time instead of being rebuilt.
	class Session
	{
	public:
		void Append(const std::vector<TranscriptRecord>& records);

		const std::vector<Turn>& GetTurns() const { return m_Turns; }
		const std::vector<ToolCall>& GetTools() const { return m_Tools; }

		// Moves whenever an append changed anything, so a view can tell its layout is stale.
		std::uint64_t GetVersion() const { return m_Version; }

	private:
		Turn& CurrentTurn();
		void AddAssistant(const TranscriptRecord& record);
		void AddUser(const TranscriptRecord& record);

		std::vector<Turn> m_Turns;
		std::vector<ToolCall> m_Tools;
		std::unordered_map<std::string, std::size_t> m_ToolById;
		std::unordered_set<std::string> m_SeenRequests;
		std::uint64_t m_Version = 0;
	};
}
