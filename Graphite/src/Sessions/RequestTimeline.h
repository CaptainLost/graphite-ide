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
		Reminder,

		// An image the user pasted. It is sized here by the bytes it decodes to, which is
		// not what it is billed by: an image's cost is its pixels, not its bytes.
		Image,

		// What the session's first request carried ahead of every message, from Claude
		// Code's snapshot of its prompt: the system prompt, and the tools it defined in full.
		SystemPrompt,
		ToolSchemas
	};

	struct SentItem
	{
		SentKind Kind = SentKind::Reminder;
		std::string Label;
		std::string ToolName; // ToolResult only
		bool IsError = false; // ToolResult only
		std::size_t Bytes = 0;

		// What the model was sent, as plain text: an image is a placeholder naming it.
		std::string Text;

		// Text was reconstructed from a transcript too old to record what was sent.
		bool Approximate = false;
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

		// What the model wrote: the thinking, the reply, or a tool's name over its input
		// laid out as JSON.
		std::string Text;
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
		std::size_t ToolsLoaded = 0;    // names this request's own tools deltas added
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
		// A tool_use block's name and the summary of its input, kept so the tool_result answering it — and the
		// request that carries that result — can name it and summarise it.
		struct ToolCallInfo
		{
			std::string Name;
			std::string Summary;
		};

		void AddAssistant(const TranscriptRecord& record, bool& changed);
		void AddUser(const TranscriptRecord& record, bool& changed);
		void AddAttachment(const TranscriptRecord& record, bool& changed);
		void AddSnapshot(const TranscriptRecord& record, bool& changed);

		// Replaces whatever the snapshot put at the head of `request` with m_Snapshot.
		void PlaceSnapshot(ApiRequest& request) const;

		void ApplyUsage(ApiRequest& request, std::size_t index, const TokenCounts& usage);

		// The delta for the request at `index`: its Context against the Context of the last
		// earlier request that had one, or nothing when none did or this one has none.
		std::optional<std::int64_t> ContextDeltaFor(std::size_t index) const;

		std::vector<ApiRequest> m_Requests;
		std::unordered_map<std::string, std::size_t> m_ByKey;

		// Whether the request at the same index has adopted a usage yet. A request created
		// from a record with no usage starts false and may turn true later, when a further
		// record of the same key carries one.
		std::vector<bool> m_HasUsage;

		// Sent items seen since the last response, waiting for the request they belong to.
		std::vector<SentItem> m_Pending;

		// What the latest prompt snapshot says the first request carried ahead of every
		// message: its system prompt and, once Claude Code has written them, its tool schemas.
		// Claude Code writes the second snapshot after that request's response, so the items
		// are placed on a request that already exists as often as on one still to come.
		std::vector<SentItem> m_Snapshot;

		std::unordered_map<std::string, ToolCallInfo> m_ToolCalls;

		// The deferred tools loaded so far, as of the last attachment that changed them.
		std::set<std::string> m_Tools;

		// Names the tools deltas in m_Pending added, for the request they go out with.
		std::size_t m_PendingLoaded = 0;

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

	// "Tools available: 12 (+3 loaded)", the count in brackets only when the request's own
	// deltas loaded any.
	std::string ToolsAvailableLabel(const ApiRequest& request);
}
