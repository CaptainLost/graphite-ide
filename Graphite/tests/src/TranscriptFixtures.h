#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

// Lines in the shape Claude Code writes them, built through the JSON library so escaping is
// never hand-written. Only the fields the model reads are present; real records carry many more.
namespace Fixtures {

	inline std::string Line(const nlohmann::json& record)
	{
		return record.dump() + "\n";
	}

	inline nlohmann::json Usage(std::uint64_t input, std::uint64_t output, std::uint64_t write5m = 0,
								std::uint64_t write1h = 0, std::uint64_t read = 0,
								const char* speed = "standard")
	{
		return {{"input_tokens", input},
				{"output_tokens", output},
				{"cache_creation_input_tokens", write5m + write1h},
				{"cache_read_input_tokens", read},
				{"cache_creation",
				 {{"ephemeral_5m_input_tokens", write5m}, {"ephemeral_1h_input_tokens", write1h}}},
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
			message["stop_reason"] = stopReason;

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

	// A tool result carrying the record-level `toolUseResult` Claude Code writes beside it:
	// the structured facts (line counts, patches, sizes) the conversation view reads.
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

	// A message the user typed while Claude was still working: Claude Code queues it and
	// writes it as an attachment rather than a user record.
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

	inline std::string AiTitle(const std::string& title)
	{
		return Line({{"type", "ai-title"}, {"aiTitle", title}, {"sessionId", "s"}});
	}
}
