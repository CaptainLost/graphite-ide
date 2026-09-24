#pragma once

#include <algorithm>
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

		// Only ever takes back what was added before, so no bucket goes below zero.
		TokenCounts& operator-=(const TokenCounts& other)
		{
			Input -= other.Input;
			Output -= other.Output;
			CacheWrite5m -= other.CacheWrite5m;
			CacheWrite1h -= other.CacheWrite1h;
			CacheRead -= other.CacheRead;
			return *this;
		}

		std::uint64_t Total() const
		{
			return Input + Output + CacheWrite5m + CacheWrite1h + CacheRead;
		}

		friend bool operator==(const TokenCounts&, const TokenCounts&) = default;
	};

	// The usage of a response seen in two of its records: the larger count in every bucket.
	// A record written while the response streamed carries the output generated so far, and
	// the response's last record carries all of it. A refused response is followed by a record
	// Claude Code writes itself under the same request id with every bucket zero. The largest
	// count is the right one in both cases.
	inline TokenCounts MergeResponseUsage(const TokenCounts& a, const TokenCounts& b)
	{
		return {std::max(a.Input, b.Input), std::max(a.Output, b.Output), std::max(a.CacheWrite5m, b.CacheWrite5m),
				std::max(a.CacheWrite1h, b.CacheWrite1h), std::max(a.CacheRead, b.CacheRead)};
	}

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
		Image,
		Other
	};

	struct ContentBlock
	{
		BlockKind Kind = BlockKind::Other;

		// The text, the thinking, a tool result's text, an image's media type ("image/png"), or
		// — for Other — the block's type name.
		std::string Text;

		// A tool_use's own id, or the id a tool_result answers.
		std::string ToolUseId;
		std::string ToolName;

		// A tool_use's input, serialised again so the model holds no JSON type.
		std::string InputJson;

		// For a tool_result: the record's `toolUseResult`, serialised — the structured facts
		// (line counts, patches, output sizes) that the text of the result does not carry.
		// Empty when the record has none.
		std::string ResultJson;

		bool IsError = false;

		// For an image: the length of its base64 data. The data itself is not kept — a pasted
		// screenshot is hundreds of kilobytes the views never draw.
		std::size_t DataBytes = 0;
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

		// A message the user typed while Claude was working, which Claude Code queues and
		// writes as a `queued_command` attachment. Empty for every other record, including a
		// queued command the user did not type (a hook's, say). A message with a pasted image
		// has a placeholder line naming the image, as the requests' sent text does.
		std::string QueuedPrompt;

		// Text Claude Code injected as if the user had said it (a skill's body, a reminder, a
		// compaction summary): it is not a prompt the user typed.
		bool IsMeta = false;

		// The summary Claude Code writes in place of the context it compacted. It is a user
		// record with no isMeta, so this flag is the only thing that says no one typed it.
		bool IsCompactSummary = false;

		std::vector<ContentBlock> Blocks;
		std::optional<TokenCounts> Usage;
		bool Fast = false;

		// Why an assistant response stopped (`message.stop_reason`): "end_turn", "tool_use", ...
		std::string StopReason;

		// The names an attachment added: a deferred tool or MCP instructions delta's
		// `addedNames`, or a skill listing's `names`.
		std::vector<std::string> AttachmentNames;

		// The names a deferred tools delta's `removedNames` took away.
		std::vector<std::string> AttachmentRemoved;

		// The file an `edited_text_file` attachment names.
		std::string AttachmentFile;

		// The line's length, excluding the newline and any trailing '\r'.
		std::size_t Bytes = 0;

		// For an attachment: the text it put in front of the model — read from `rendered`
		// when Claude Code wrote it, reconstructed from the attachment's own fields when it
		// did not.
		std::string AttachmentText;

		// False for an attachment the model never saw: from Claude Code 2.1.260 on, one that
		// carries no `rendered` is Claude Code's own bookkeeping (a hook's outcome, a snapshot
		// of the prompt).
		bool AttachmentSent = true;

		// AttachmentText was reconstructed rather than read: the record predates `rendered`,
		// or does not say which version wrote it.
		bool AttachmentApproximate = false;

		// For a `prompt_snapshot`: the system prompt the session's first request went out
		// with — the CLI prefix, then each block, a blank line between — or empty when the
		// snapshot holds none.
		std::string SystemPromptText;

		// For a `prompt_snapshot`: every tool schema the first request carried in full, each
		// as its name, its description and its input schema, a rule between tools.
		std::string ToolSchemasText;
		std::size_t ToolSchemaCount = 0;
	};
}
