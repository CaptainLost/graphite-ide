#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

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

		// WARNING: Only ever takes back what was added before; the unsigned buckets wrap below zero.
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

	inline TokenCounts MergeResponseUsage(const TokenCounts& first, const TokenCounts& second)
	{
		return {std::max(first.Input, second.Input), std::max(first.Output, second.Output),
				std::max(first.CacheWrite5m, second.CacheWrite5m), std::max(first.CacheWrite1h, second.CacheWrite1h),
				std::max(first.CacheRead, second.CacheRead)};
	}

	struct CompactionFacts
	{
		std::string Trigger;
		std::optional<std::uint64_t> PreTokens;
		std::optional<std::uint64_t> PostTokens;
		std::optional<std::chrono::milliseconds> Duration;
	};

	enum class RecordKind
	{
		User,
		Assistant,
		Attachment,
		System,
		AiTitle,
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
		std::string Text;
		std::string ToolUseId;
		std::string ToolName;
		std::string InputJson;
		std::string ResultJson;
		bool IsError = false;
		std::size_t DataBytes = 0;
	};

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
		std::string QueuedPrompt;
		bool IsMeta = false;
		bool IsCompactSummary = false;
		bool IsCompactBoundary = false;
		CompactionFacts Compaction;

		std::vector<ContentBlock> Blocks;
		std::optional<TokenCounts> Usage;
		bool Fast = false;
		std::string StopReason;
		std::vector<std::string> AttachmentNames;
		std::vector<std::string> AttachmentRemoved;
		std::string AttachmentFile;
		std::size_t Bytes = 0;
		std::string AttachmentText;
		bool AttachmentSent = true;
		bool AttachmentApproximate = false;
		std::string SystemPromptText;
		std::string ToolSchemasText;
		std::size_t ToolSchemaCount = 0;
	};
}
