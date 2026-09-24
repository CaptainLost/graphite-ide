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
		std::string Label;    // Text: "N chars"; ToolCall: tool name + "  " + summary; Thinking: "thinking (empty)"
							  // when Text is empty, else "thinking (N chars)"
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
		std::string RespondedAt; // the response's first record

		// When the request went out, read as the last record written before its response: the
		// prompt, or the last tool result or attachment it carries. Empty when nothing preceded it.
		std::string SentAt;

		// The response's last record. A response is written a record per block, and the model
		// is still writing it until the last one: Claude Code may already have run a tool the
		// response called, and written its result, between two of them.
		std::string FinishedAt;

		// From SentAt to FinishedAt: the whole time the model took, not only to its first block.
		std::optional<std::chrono::milliseconds> Waited;
		TokenCounts Usage;
		std::uint64_t Context = 0; // input + cache writes + cache read
		std::optional<std::int64_t> ContextDelta;
		double CachedShare = 0.0; // 0..1
		bool CacheRebuilt = false; // cache write >= half of Context
		std::vector<SentItem> Sent;
		std::vector<ResponseItem> Response;
		std::string StopReason;
		std::size_t ToolsAvailable = 0; // deferred tool names announced by this request
		std::size_t ToolsLoaded = 0;    // new tool names this request's own deltas announced

		// Tools the request carried with their full schema, from the session's latest prompt
		// snapshot: the tool count a reader can trust, unlike ToolsAvailable's names alone.
		std::size_t ToolSchemas = 0;
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

		// The latest snapshot's tool schema count, applied to every request: the tool set with
		// full schemas is the session's, not any one request's.
		std::size_t m_SnapshotToolSchemas = 0;

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

	// "Tools: 17 with schemas, 88 by name only (+3 new)": the tools this request had in full
	// against the ones it only had a deferred name for, the count in brackets only when the
	// request's own deltas announced any. Either count is left out when it is zero, and both
	// being zero reads as "Tools: none recorded".
	std::string ToolsAvailableLabel(const ApiRequest& request);

	// What the Sent section's heading says: "Added to the context", or, when Sent is empty,
	// "Nothing added to the context" — the same wording for every request, #1 included.
	std::string SentHeading(const ApiRequest& request);

	// The reference the items are new against, then the existing count and size:
	// "first request · 20 items · 190.0 KB" (#1), "since #140 · 1 item · 5.4 KB" (later requests,
	// the number is Index - 1). When Sent is empty, just the reference: "first request" or
	// "since #140".
	std::string SentSummary(const ApiRequest& request);

	// "Model's reply", or "Model's reply — nothing yet" when Response is empty.
	std::string ReplyHeading(const ApiRequest& request);

	// The Response section's parts joined by " · ", each present only when Response has one:
	// "thinking" (or "<N> thinking blocks" past one), "text" (or "<N> texts" past one), "<N> tool
	// call(s)", "<N> other" for ResponseKind::Other. E.g. "thinking · 1 tool call",
	// "thinking · 2 texts · 3 tool calls". Empty when Response is empty.
	std::string ReplySummary(const ApiRequest& request);
}
