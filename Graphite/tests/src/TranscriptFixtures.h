#pragma once

#include "Sessions/Session.h"
#include "Sessions/Transcript/TranscriptReader.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Fixtures {

	inline std::string Line(const nlohmann::json& record)
	{
		return record.dump() + "\n";
	}

	inline nlohmann::json Usage(std::uint64_t inputTokens, std::uint64_t outputTokens,
								std::uint64_t fiveMinuteCacheWriteTokens = 0,
								std::uint64_t oneHourCacheWriteTokens = 0,
								std::uint64_t cacheReadTokens = 0, const char* speed = "standard")
	{
		return {{"input_tokens", inputTokens},
				{"output_tokens", outputTokens},
				{"cache_creation_input_tokens", fiveMinuteCacheWriteTokens + oneHourCacheWriteTokens},
				{"cache_read_input_tokens", cacheReadTokens},
				{"cache_creation",
				 {{"ephemeral_5m_input_tokens", fiveMinuteCacheWriteTokens},
				  {"ephemeral_1h_input_tokens", oneHourCacheWriteTokens}}},
				{"speed", speed}};
	}

	inline nlohmann::json TextBlock(const std::string& text)
	{
		return {{"type", "text"}, {"text", text}};
	}

	inline nlohmann::json ThinkingBlock(const std::string& text)
	{
		return {{"type", "thinking"}, {"thinking", text}};
	}

	inline nlohmann::json ToolUseBlock(const std::string& id, const std::string& name,
									   const nlohmann::json& input)
	{
		return {{"type", "tool_use"}, {"id", id}, {"name", name}, {"input", input}};
	}

	inline nlohmann::json ImageBlock(const std::string& mediaType, const std::string& base64)
	{
		return {{"type", "image"}, {"source", {{"type", "base64"}, {"media_type", mediaType}, {"data", base64}}}};
	}

	inline std::string UserBlocks(const nlohmann::json& blocks,
								  const std::string& timestamp = "2026-09-17T09:00:00.000Z")
	{
		return Line({{"type", "user"},
					 {"uuid", "user-" + timestamp},
					 {"timestamp", timestamp},
					 {"cwd", "D:\\Repos\\IDE"},
					 {"message", {{"role", "user"}, {"content", blocks}}}});
	}

	inline std::string UserPrompt(const std::string& text,
								  const std::string& timestamp = "2026-09-17T09:00:00.000Z",
								  const std::string& cwd = "D:\\Repos\\IDE")
	{
		return Line({{"type", "user"},
					 {"uuid", "user-" + timestamp},
					 {"timestamp", timestamp},
					 {"cwd", cwd},
					 {"message", {{"role", "user"}, {"content", text}}}});
	}

	inline std::string MetaUser(const std::string& text,
								const std::string& timestamp = "2026-09-17T09:00:00.500Z")
	{
		return Line({{"type", "user"},
					 {"isMeta", true},
					 {"timestamp", timestamp},
					 {"message", {{"role", "user"}, {"content", text}}}});
	}

	inline std::string Assistant(const std::string& requestId, const nlohmann::json& block,
								 const nlohmann::json& usage,
								 const std::string& model = "claude-opus-5",
								 const std::string& timestamp = "2026-09-17T09:00:01.000Z",
								 const char* stopReason = nullptr)
	{
		nlohmann::json message = {{"model", model},
								  {"role", "assistant"},
								  {"content", nlohmann::json::array({block})},
								  {"usage", usage}};

		if (stopReason != nullptr)
		{
			message["stop_reason"] = stopReason;
		}

		return Line({{"type", "assistant"},
					 {"uuid", "assistant-" + requestId + "-" + timestamp},
					 {"requestId", requestId},
					 {"timestamp", timestamp},
					 {"cwd", "D:\\Repos\\IDE"},
					 {"message", message}});
	}

	inline std::string ToolResult(const std::string& toolUseId, const std::string& text,
								  bool isError = false,
								  const std::string& timestamp = "2026-09-17T09:00:02.000Z")
	{
		return Line({{"type", "user"},
					 {"timestamp", timestamp},
					 {"message",
					  {{"role", "user"},
					   {"content", nlohmann::json::array({{{"type", "tool_result"},
														   {"tool_use_id", toolUseId},
														   {"content", text},
														   {"is_error", isError}}})}}}});
	}

	inline std::string ToolResultWith(const std::string& toolUseId, const std::string& text,
									  const nlohmann::json& result, bool isError = false,
									  const std::string& timestamp = "2026-09-17T09:00:02.000Z")
	{
		return Line({{"type", "user"},
					 {"timestamp", timestamp},
					 {"toolUseResult", result},
					 {"message",
					  {{"role", "user"},
					   {"content", nlohmann::json::array({{{"type", "tool_result"},
														   {"tool_use_id", toolUseId},
														   {"content", text},
														   {"is_error", isError}}})}}}});
	}

	inline std::string QueuedCommand(const std::string& prompt,
									 const std::string& timestamp = "2026-09-17T09:00:03.000Z",
									 const std::string& originKind = "human",
									 const std::string& commandMode = "prompt")
	{
		return Line({{"type", "attachment"},
					 {"uuid", "queued-" + timestamp},
					 {"timestamp", timestamp},
					 {"attachment",
					  {{"type", "queued_command"},
					   {"prompt", prompt},
					   {"origin", {{"kind", originKind}}},
					   {"commandMode", commandMode}}}});
	}

	inline std::string Attachment(const nlohmann::json& attachment,
								  const std::string& timestamp = "2026-09-17T09:00:00.500Z")
	{
		return Line({{"type", "attachment"}, {"timestamp", timestamp}, {"attachment", attachment}});
	}

	inline std::string RenderedAttachment(const nlohmann::json& attachment, const std::vector<std::string>& rendered,
										  const std::string& version = "2.1.280",
										  const std::string& timestamp = "2026-09-17T09:00:00.500Z")
	{
		nlohmann::json parts = nlohmann::json::array();

		for (const std::string& text : rendered)
		{
			parts.push_back(nlohmann::json{{"content", text}});
		}

		return Line({{"type", "attachment"},
					 {"version", version},
					 {"timestamp", timestamp},
					 {"attachment", attachment},
					 {"rendered", parts}});
	}

	inline std::string VersionedAttachment(const nlohmann::json& attachment, const std::string& version,
										   const std::string& timestamp = "2026-09-17T09:00:00.500Z")
	{
		return Line({{"type", "attachment"}, {"version", version}, {"timestamp", timestamp}, {"attachment", attachment}});
	}

	inline std::string AiTitle(const std::string& title)
	{
		return Line({{"type", "ai-title"}, {"aiTitle", title}, {"sessionId", "s"}});
	}

	// WARNING: The first record was written while the response still streamed, so it carries the
	// input and cache counts but only the output generated so far; the last carries all of it.
	inline const std::string kStreamedResponse =
		R"({"type": "assistant", "uuid": "74674d8b-a7b6-49e3-a1ab-fcd8e2fa3578", "requestId": "req_011CeXHAwLduK1tMsKBgq5eP", "timestamp": "2026-08-29T15:31:00.692Z", "version": "2.1.251", "message": {"model": "claude-opus-5", "role": "assistant", "content": [{"type": "thinking", "thinking": "", "signature": "sig"}], "stop_reason": null, "usage": {"input_tokens": 2, "cache_creation_input_tokens": 304, "cache_read_input_tokens": 67877, "cache_creation": {"ephemeral_5m_input_tokens": 304, "ephemeral_1h_input_tokens": 0}, "output_tokens": 2}}})"
		"\n"
		R"({"type": "assistant", "uuid": "92511126-51e6-4b5a-80df-bc865627cf2d", "requestId": "req_011CeXHAwLduK1tMsKBgq5eP", "timestamp": "2026-08-29T15:31:01.429Z", "version": "2.1.251", "message": {"model": "claude-opus-5", "role": "assistant", "content": [{"type": "tool_use", "id": "toolu_01H1K4TrABd9YsVBXCRvrB1b", "name": "Bash", "input": {"command": "ls"}, "caller": {"type": "direct"}}], "stop_reason": "tool_use", "usage": {"input_tokens": 2, "cache_creation_input_tokens": 304, "cache_read_input_tokens": 67877, "output_tokens": 657, "output_tokens_details": {"thinking_tokens": 487}, "cache_creation": {"ephemeral_1h_input_tokens": 0, "ephemeral_5m_input_tokens": 304}}}})"
		"\n";

	// WARNING: Claude Code follows the model's record with one of its own under the same request
	// id, naming the <synthetic> model with all-zero usage; the request cost what the first says.
	inline const std::string kRefusedResponse =
		R"({"type": "assistant", "uuid": "cfbefb86-0467-4fe6-9dc8-4dbafc8b6cc0", "requestId": "req_011CeXG5cyqF3ePoWdsANZSi", "timestamp": "2026-08-29T15:16:50.435Z", "version": "2.1.251", "message": {"model": "claude-opus-4-8", "role": "assistant", "content": [{"type": "thinking", "thinking": "", "signature": "sig"}], "stop_reason": "refusal", "usage": {"input_tokens": 2, "cache_creation_input_tokens": 17298, "cache_read_input_tokens": 28871, "output_tokens": 1066, "output_tokens_details": {"thinking_tokens": 1066}, "cache_creation": {"ephemeral_1h_input_tokens": 17298, "ephemeral_5m_input_tokens": 0}}}})"
		"\n"
		R"({"type": "assistant", "uuid": "1dd9a222-f991-4296-a9a1-f34e8eae8514", "requestId": "req_011CeXG5cyqF3ePoWdsANZSi", "timestamp": "2026-08-29T15:16:50.453Z", "isApiErrorMessage": true, "version": "2.1.251", "message": {"model": "<synthetic>", "role": "assistant", "content": [{"type": "text", "text": "API Error: Opus 4.8's safeguar"}], "stop_reason": "refusal", "usage": {"output_tokens_details": null, "input_tokens": 0, "output_tokens": 0, "cache_creation_input_tokens": 0, "cache_read_input_tokens": 0, "cache_creation": {"ephemeral_1h_input_tokens": 0, "ephemeral_5m_input_tokens": 0}}}})"
		"\n";

	// WARNING: The summary is a user record with a plain string for its content and no isMeta;
	// only isCompactSummary says the user never typed it.
	inline const std::string kCompactSummary =
		R"({"type": "user", "uuid": "c1fc7cb3-0c0f-4efa-bc2a-8baf1f6ad687", "timestamp": "2026-08-31T16:33:10.124Z", "isCompactSummary": true, "isVisibleInTranscriptOnly": true, "version": "2.1.251", "message": {"role": "user", "content": "This session is being continued from a previous conversation that ran out of context. The summary below covers the earli"}})"
		"\n";

	inline const std::string kCompactBoundary =
		R"({"type": "system", "subtype": "compact_boundary", "content": "Conversation compacted", "level": "info", "uuid": "144b7ad8-b082-4bd9-9bea-04c0f5dca872", "logicalParentUuid": "fea75cdd-d071-4a6e-8e96-63efa87af5b3", "timestamp": "2026-08-31T16:33:10.464Z", "version": "2.1.251", "compactMetadata": {"trigger": "auto", "preTokens": 167324, "postTokens": 5090, "cumulativeDroppedTokens": 162234, "durationMs": 169810, "preservedSegment": {"headUuid": "308f8b8a-efb4-4fe2-8d78-1e5e4e406729", "anchorUuid": "c1fc7cb3-0c0f-4efa-bc2a-8baf1f6ad687", "tailUuid": "fea75cdd-d071-4a6e-8e96-63efa87af5b3"}, "preservedMessages": {"anchorUuid": "c1fc7cb3-0c0f-4efa-bc2a-8baf1f6ad687", "uuids": ["308f8b8a-efb4-4fe2-8d78-1e5e4e406729"], "allUuids": ["308f8b8a-efb4-4fe2-8d78-1e5e4e406729"]}}, "parentUuid": null})"
		"\n";

	inline Graphite::Session SessionFromTranscript(const std::string& bytes)
	{
		Graphite::Session session;
		session.Append(Graphite::ParseTranscript(bytes).Records);
		return session;
	}
}
