#pragma once

#include "Sessions/Transcript/TranscriptRecord.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Graphite {

	enum class SentKind
	{
		ToolResult,
		Prompt,
		QueuedPrompt,
		Injected,
		ToolsLoaded,
		Skills,
		McpInstructions,
		EditedFile,
		Reminder,

		// WARNING: An image is sized by the bytes it decodes to, which is not what it is billed by:
		// an image costs its pixels, not its bytes.
		Image,

		SystemPrompt,
		ToolSchemas
	};

	struct SentItem
	{
		SentKind Kind = SentKind::Reminder;
		std::string Label;
		std::string ToolName;
		bool IsError = false;
		std::size_t Bytes = 0;

		std::string Text;

		bool Approximate = false;
	};

	enum class ResponseKind
	{
		Thinking,
		Text,
		ToolCall,
		Other
	};

	struct ResponseItem
	{
		ResponseKind Kind = ResponseKind::Other;
		std::string Label;
		std::string ToolName;

		std::string Text;
	};

	struct ApiRequest
	{
		std::size_t Index = 0;
		std::string Key;
		std::string Model;
		bool Fast = false;
		std::string RespondedAt;

		std::string SentAt;

		std::string FinishedAt;

		std::optional<std::chrono::milliseconds> Waited;
		TokenCounts Usage;
		std::uint64_t Context = 0;
		std::optional<std::int64_t> ContextDelta;
		double CachedShare = 0.0;
		bool CacheRebuilt = false;
		std::vector<SentItem> Sent;
		std::vector<ResponseItem> Response;
		std::string StopReason;
		std::size_t ToolsAvailable = 0;
		std::size_t ToolsLoaded = 0;

		std::size_t ToolSchemas = 0;
	};

	class RequestTimeline
	{
	public:
		void Append(const std::vector<TranscriptRecord>& records);

		void Reset();

		const std::vector<ApiRequest>& GetRequests() const { return m_Requests; }

		std::optional<std::size_t> IndexOf(std::string_view key) const;

		std::uint64_t GetVersion() const { return m_Version; }

	private:
		struct ToolCallInfo
		{
			std::string Name;
			std::string Summary;
		};

		void AddAssistant(const TranscriptRecord& record, bool& changed);
		void AddUser(const TranscriptRecord& record, bool& changed);
		void AddAttachment(const TranscriptRecord& record, bool& changed);
		void AddSnapshot(const TranscriptRecord& record, bool& changed);

		void PlaceSnapshot(ApiRequest& request) const;

		void ApplyUsage(ApiRequest& request, std::size_t index, const TokenCounts& usage);

		std::optional<std::int64_t> ContextDeltaFor(std::size_t index) const;

		std::vector<ApiRequest> m_Requests;
		std::unordered_map<std::string, std::size_t> m_ByKey;

		std::vector<bool> m_HasUsage;

		std::vector<SentItem> m_Pending;

		std::vector<SentItem> m_Snapshot;

		std::size_t m_SnapshotToolSchemas = 0;

		std::unordered_map<std::string, ToolCallInfo> m_ToolCalls;

		std::set<std::string> m_Tools;

		std::size_t m_PendingLoaded = 0;

		// WARNING: Holds the last timestamp that parsed. A malformed one kept here would make every
		// Waited after it uncomputable.
		std::string m_LastTimestamp;
		std::uint64_t m_Version = 0;
	};

	std::vector<ApiRequest> BuildRequestTimeline(const std::vector<TranscriptRecord>& records);

	std::vector<std::string> ToolNamesUsed(const ApiRequest& request);

	std::string ToolsAvailableLabel(const ApiRequest& request);

	std::string SentHeading(const ApiRequest& request);

	std::string SentSummary(const ApiRequest& request);

	std::string ReplyHeading(const ApiRequest& request);

	std::string ReplySummary(const ApiRequest& request);
}
