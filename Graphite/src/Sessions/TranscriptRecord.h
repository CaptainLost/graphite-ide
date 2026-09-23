#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// The buckets a request is billed in. Cache writes are split by lifetime because the
	// hour-long one costs more than the five-minute one.
	struct TokenCounts
	{
		std::uint64_t Input = 0;
		std::uint64_t Output = 0;
		std::uint64_t CacheWrite5m = 0;
		std::uint64_t CacheWrite1h = 0;
		std::uint64_t CacheRead = 0;

		TokenCounts& operator+=(const TokenCounts& other)
		{
			Input += other.Input;
			Output += other.Output;
			CacheWrite5m += other.CacheWrite5m;
			CacheWrite1h += other.CacheWrite1h;
			CacheRead += other.CacheRead;
			return *this;
		}

		std::uint64_t Total() const
		{
			return Input + Output + CacheWrite5m + CacheWrite1h + CacheRead;
		}

		friend bool operator==(const TokenCounts&, const TokenCounts&) = default;
	};

	enum class RecordKind
	{
		User,
		Assistant,
		Attachment,
		System,
		AiTitle,
		// Every type this reader does not model. Claude Code adds record types between
		// versions; a transcript from a newer one must still read.
		Other
	};

	enum class BlockKind
	{
		Text,
		Thinking,
		ToolUse,
		ToolResult,
		Other
	};

	struct ContentBlock
	{
		BlockKind Kind = BlockKind::Other;

		// The text, the thinking, a tool result's text, or — for Other — the block's type name.
		std::string Text;

		// A tool_use's own id, or the id a tool_result answers.
		std::string ToolUseId;
		std::string ToolName;

		// A tool_use's input, serialised again so the model holds no JSON type.
		std::string InputJson;

		bool IsError = false;
	};

	// One line of a transcript, reduced to the fields this feature reads.
	struct TranscriptRecord
	{
		RecordKind Kind = RecordKind::Other;
		std::string TypeName;
		std::string Uuid;
		std::string Timestamp;
		std::string Cwd;
		std::string RequestId;
		std::string Model;
		std::string AttachmentType;
		std::string Title;

		// Text Claude Code injected as if the user had said it (a skill's body, a reminder):
		// it is not a prompt the user typed.
		bool IsMeta = false;

		std::vector<ContentBlock> Blocks;
		std::optional<TokenCounts> Usage;
		bool Fast = false;
	};
}
