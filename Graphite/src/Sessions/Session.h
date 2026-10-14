#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Transcript/TranscriptRecord.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
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

		Queued,

		Compaction,
		Other
	};

	struct TurnItem
	{
		ItemKind Kind = ItemKind::Other;

		std::string Text;

		std::size_t Tool = 0;

		std::string Label;

		std::string At;

		std::string RequestId;

		CompactionFacts Compaction;
	};

	struct RequestUsage
	{
		std::string Model;
		bool Fast = false;
		TokenCounts Usage;
	};

	struct Turn
	{
		std::string Prompt;
		std::string StartedAt;
		std::vector<TurnItem> Items;
		UsageByModel Usage;
		std::map<std::string, RequestUsage> Requests;
	};

	class Session
	{
	public:
		void Append(const std::vector<TranscriptRecord>& records);

		void Reset();

		const std::vector<Turn>& GetTurns() const { return m_Turns; }
		const std::vector<ToolCall>& GetTools() const { return m_Tools; }

		std::uint64_t GetVersion() const { return m_Version; }

		// WARNING: GetVersion() is not monotonic across a Reset, so a view tells a restart from growth by this.
		std::uint64_t GetGeneration() const { return m_Generation; }

		// WARNING: timestamps share one fixed-width UTC format, so the latest is the greatest string.
		const std::string& GetLastWrittenAt() const { return m_LastWrittenAt; }

	private:
		Turn& CurrentTurn();
		void AddAssistant(const TranscriptRecord& record);
		void AddUser(const TranscriptRecord& record);
		void AddQueued(const TranscriptRecord& record);
		void AddCompaction(const TranscriptRecord& record);

		void AttachCompactSummary(const std::string& text, const std::string& at);

		std::vector<Turn> m_Turns;
		std::vector<ToolCall> m_Tools;
		std::unordered_map<std::string, std::size_t> m_ToolById;
		std::unordered_map<std::string, std::size_t> m_RequestTurn;
		std::string m_LastWrittenAt;
		std::uint64_t m_Version = 0;
		std::uint64_t m_Generation = 0;
	};
}
