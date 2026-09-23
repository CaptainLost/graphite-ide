#pragma once

#include "Sessions/TranscriptRecord.h"

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

	// What one item sent to the model was: a tool's result, the words the user typed, or
	// bookkeeping Claude Code adds on its own (a loaded tool, a skill, a reminder).
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
		Reminder
	};

	struct SentItem
	{
		SentKind Kind = SentKind::Reminder;
		std::string Label;
		std::string ToolName; // ToolResult only
		bool IsError = false; // ToolResult only
		std::size_t Bytes = 0;
		std::int64_t EstimatedTokens = 0;
	};

	// What one block of a response was.
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
		std::string Label;    // Text: "N chars"; ToolCall: tool name + "  " + summary; Thinking: "thinking" or "thinking (N chars)"
		std::string ToolName; // ToolCall only
	};

	// One call to the model: everything queued for it since the previous response, and what it
	// answered with, however many transcript records the response itself took to write.
	struct ApiRequest
	{
		std::size_t Index = 0; // 1-based
		std::string Key;       // requestId, else uuid
		std::string Model;
		bool Fast = false;
		std::string RespondedAt;
		std::optional<std::chrono::milliseconds> Waited;
		TokenCounts Usage;
		std::uint64_t Context = 0; // input + cache writes + cache read
		std::optional<std::int64_t> ContextDelta;
		double CachedShare = 0.0; // 0..1
		bool CacheRebuilt = false; // cache write >= half of Context
		std::vector<SentItem> Sent;
		std::vector<ResponseItem> Response;
		std::string StopReason;
		std::size_t ToolsAvailable = 0; // deferred tools loaded at this request
	};

	// Cuts a transcript's records into the API requests it made. Built by appending, like
	// Session, so a live transcript grows without being rebuilt from nothing.
	class RequestTimeline
	{
	public:
		void Append(const std::vector<TranscriptRecord>& records);

		// Clears every request and everything derived from them, and moves to a new version.
		void Reset();

		const std::vector<ApiRequest>& GetRequests() const { return m_Requests; }

		// The request's position in GetRequests(), by its Key.
		std::optional<std::size_t> IndexOf(std::string_view key) const;

		// Moves whenever an append or a reset changed anything, so a view can tell its layout
		// is stale.
		std::uint64_t GetVersion() const { return m_Version; }

	private:
		// A tool_use block's name and input, kept so the tool_result answering it — and the
		// request that carries that result — can name it and summarise it.
		struct ToolCallInfo
		{
			std::string Name;
			std::string InputJson;
		};

		void AddAssistant(const TranscriptRecord& record, bool& changed);
		void AddUser(const TranscriptRecord& record, bool& changed);
		void AddAttachment(const TranscriptRecord& record, bool& changed);
		void EstimateTokens(ApiRequest& request) const;
		void ApplyUsage(ApiRequest& request, std::size_t index, const TokenCounts& usage);

		// The delta baseline for the request at `index`: its Context against the Context of
		// the last earlier request that had usage, or nothing when none did.
		std::optional<std::int64_t> ContextDeltaFor(std::size_t index) const;

		std::vector<ApiRequest> m_Requests;
		std::unordered_map<std::string, std::size_t> m_ByKey;

		// Whether the request at the same index has adopted a usage yet. A request created
		// from a record with no usage starts false and may turn true later, when a further
		// record of the same key carries one.
		std::vector<bool> m_HasUsage;

		// Sent items seen since the last response, waiting for the request they belong to.
		std::vector<SentItem> m_Pending;

		std::unordered_map<std::string, ToolCallInfo> m_ToolCalls;

		// The deferred tools loaded so far, as of the last attachment that changed them.
		std::set<std::string> m_Tools;

		// The last record's timestamp that actually parsed. A malformed one is worse than no
		// timestamp: kept, it would make every Waited after it uncomputable instead of just
		// the one record that carried it.
		std::string m_LastTimestamp;
		std::uint64_t m_Version = 0;
	};

	std::vector<ApiRequest> BuildRequestTimeline(const std::vector<TranscriptRecord>& records);

	// The tools this request used: called in its Response, or whose results it Sent — unique,
	// in first-seen order.
	std::vector<std::string> ToolNamesUsed(const ApiRequest& request);
}
