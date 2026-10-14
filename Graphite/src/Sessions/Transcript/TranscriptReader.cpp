#include "Sessions/Transcript/TranscriptReader.h"

#include "Sessions/Transcript/JsonAccess.h"
#include "Sessions/Transcript/ToolFacts.h"

#include "Core/Base/Ascii.h"
#include "Core/Base/Strings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	namespace {

		constexpr int kCompactJsonIndent = -1;
		constexpr int kToolSchemaJsonIndent = 2;
		constexpr char kJsonIndentCharacter = ' ';
		constexpr bool kJsonEnsureAscii = false;
		constexpr bool kParseWithoutThrowing = false;
		constexpr std::size_t kToolRuleLength = 40;
		constexpr std::uint64_t kBase64DecodedBytesPerGroup = 3;
		constexpr std::uint64_t kBase64CharactersPerGroup = 4;
		constexpr std::uint64_t kMinimumReadStep = 1;

		// WARNING: The `replace` error handler, not the default `strict`: a tool input holding
		// invalid UTF-8 must not throw out of a reader that promises not to.
		constexpr nlohmann::json::error_handler_t kJsonInvalidUtf8Handler = nlohmann::json::error_handler_t::replace;

		bool BoolAt(const nlohmann::json& object, const char* key)
		{
			const nlohmann::json::const_iterator found = object.find(key);
			return found != object.end() && found->is_boolean() && found->get<bool>();
		}

		std::vector<std::string> StringsAt(const nlohmann::json& object, const char* key)
		{
			std::vector<std::string> values;
			const nlohmann::json::const_iterator found = object.find(key);

			if (found == object.end() || !found->is_array())
			{
				return values;
			}

			for (const nlohmann::json& element : *found)
			{
				if (element.is_string())
				{
					values.push_back(element.get<std::string>());
				}
			}

			return values;
		}

		CompactionFacts CompactionOf(const nlohmann::json& recordJson)
		{
			CompactionFacts facts;
			const nlohmann::json::const_iterator metadata = recordJson.find("compactMetadata");

			if (metadata == recordJson.end() || !metadata->is_object())
			{
				return facts;
			}

			facts.Trigger = StringAt(*metadata, "trigger");
			facts.PreTokens = OptionalCountAt(*metadata, "preTokens");
			facts.PostTokens = OptionalCountAt(*metadata, "postTokens");

			if (const std::optional<std::uint64_t> durationMilliseconds = OptionalCountAt(*metadata, "durationMs"))
			{
				facts.Duration = std::chrono::milliseconds{static_cast<std::int64_t>(*durationMilliseconds)};
			}

			return facts;
		}

		RecordKind RecordKindOf(const std::string& type)
		{
			if (type == "user")
			{
				return RecordKind::User;
			}
			if (type == "assistant")
			{
				return RecordKind::Assistant;
			}
			if (type == "attachment")
			{
				return RecordKind::Attachment;
			}
			if (type == "system")
			{
				return RecordKind::System;
			}
			if (type == "ai-title")
			{
				return RecordKind::AiTitle;
			}

			return RecordKind::Other;
		}

		std::string Serialised(const nlohmann::json& value)
		{
			return value.dump(kCompactJsonIndent, kJsonIndentCharacter, kJsonEnsureAscii, kJsonInvalidUtf8Handler);
		}

		std::string ResultText(const nlohmann::json& content)
		{
			if (content.is_string())
			{
				return content.get<std::string>();
			}

			std::string joined;

			if (content.is_array())
			{
				for (const nlohmann::json& part : content)
				{
					if (part.is_object() && StringAt(part, "type") == "text")
					{
						Core::AppendSeparated(joined, "\n", StringAt(part, "text"));
					}
				}
			}

			return joined;
		}

		ContentBlock ParseBlock(const nlohmann::json& block)
		{
			ContentBlock parsed;

			if (!block.is_object())
			{
				return parsed;
			}

			const std::string type = StringAt(block, "type");

			if (type == "text")
			{
				parsed.Kind = BlockKind::Text;
				parsed.Text = StringAt(block, "text");
			}
			else if (type == "thinking")
			{
				parsed.Kind = BlockKind::Thinking;
				parsed.Text = StringAt(block, "thinking");
			}
			else if (type == "tool_use")
			{
				parsed.Kind = BlockKind::ToolUse;
				parsed.ToolUseId = StringAt(block, "id");
				parsed.ToolName = StringAt(block, "name");

				if (const nlohmann::json::const_iterator input = block.find("input"); input != block.end())
				{
					parsed.InputJson = Serialised(*input);
				}
			}
			else if (type == "tool_result")
			{
				parsed.Kind = BlockKind::ToolResult;
				parsed.ToolUseId = StringAt(block, "tool_use_id");
				parsed.IsError = BoolAt(block, "is_error");

				if (const nlohmann::json::const_iterator content = block.find("content"); content != block.end())
				{
					parsed.Text = ResultText(*content);
				}
			}
			else if (type == "image")
			{
				parsed.Kind = BlockKind::Image;

				if (const nlohmann::json::const_iterator source = block.find("source");
					source != block.end() && source->is_object())
				{
					parsed.Text = StringAt(*source, "media_type");

					if (const nlohmann::json::const_iterator data = source->find("data");
						data != source->end() && data->is_string())
					{
						parsed.DataBytes = data->get_ref<const std::string&>().size();
					}
				}
			}
			else
			{
				parsed.Kind = BlockKind::Other;
				parsed.Text = type;
			}

			return parsed;
		}

		std::optional<std::string> TextAt(const nlohmann::json& object, const char* key)
		{
			const nlohmann::json::const_iterator found = object.find(key);

			if (found == object.end())
			{
				return std::nullopt;
			}
			if (found->is_string())
			{
				return found->get<std::string>();
			}
			if (!found->is_array())
			{
				return std::nullopt;
			}

			std::vector<std::string> elementTexts;

			for (const nlohmann::json& element : *found)
			{
				elementTexts.push_back(element.is_string() ? element.get<std::string>() : Serialised(element));
			}

			return Core::JoinedWith(elementTexts, "\n");
		}

		std::string ReconstructedText(const nlohmann::json& attachment)
		{
			for (const char* key : {"text", "content", "snippet", "prompt", "addedBlocks", "addedLines"})
			{
				if (std::optional<std::string> text = TextAt(attachment, key))
				{
					return std::move(*text);
				}
			}

			return Serialised(attachment);
		}

		std::optional<std::string> RenderedText(const nlohmann::json& record)
		{
			const nlohmann::json::const_iterator found = record.find("rendered");

			if (found == record.end() || !found->is_array())
			{
				return std::nullopt;
			}

			std::vector<std::string> contents;

			for (const nlohmann::json& element : *found)
			{
				if (!element.is_object())
				{
					continue;
				}

				const nlohmann::json::const_iterator content = element.find("content");

				if (content == element.end() || !content->is_string())
				{
					continue;
				}

				contents.push_back(content->get_ref<const std::string&>());
			}

			return Core::JoinedWith(contents, "\n");
		}

		std::optional<std::string> PromptText(const nlohmann::json& attachment)
		{
			const nlohmann::json::const_iterator found = attachment.find("prompt");

			if (found == attachment.end())
			{
				return std::nullopt;
			}
			if (found->is_string())
			{
				return found->get<std::string>();
			}
			if (!found->is_array())
			{
				return std::nullopt;
			}

			std::vector<std::string> lines;

			for (const nlohmann::json& element : *found)
			{
				const ContentBlock block = ParseBlock(element);

				if (block.Kind == BlockKind::Text)
				{
					lines.push_back(block.Text);
				}
				else if (block.Kind == BlockKind::Image)
				{
					lines.push_back(ImageText(block.Text, block.DataBytes * kBase64DecodedBytesPerGroup / kBase64CharactersPerGroup));
				}
			}

			return Core::JoinedWith(lines, "\n");
		}

		constexpr std::string_view kSystemPromptCacheBoundary = "__SYSTEM_PROMPT_DYNAMIC_BOUNDARY__";

		bool IsSystemPromptCacheBoundary(std::string_view block)
		{
			constexpr std::string_view whitespace = " \t\r\n";
			return Core::WithoutSurrounding(block, whitespace) == kSystemPromptCacheBoundary;
		}

		std::string SystemPromptText(const nlohmann::json& snapshot)
		{
			std::vector<std::string> blocks = StringsAt(snapshot, "systemPrompt");
			std::erase_if(blocks, [](const std::string& block) { return IsSystemPromptCacheBoundary(block); });

			if (blocks.empty())
			{
				return {};
			}

			if (std::string prefix = StringAt(snapshot, "cliPrefix"); !prefix.empty())
			{
				blocks.insert(blocks.begin(), std::move(prefix));
			}

			return Core::JoinedWith(blocks, "\n\n");
		}

		const nlohmann::json* InputSchemaOf(const nlohmann::json& tool)
		{
			if (const nlohmann::json::const_iterator nested = tool.find("schema");
				nested != tool.end() && nested->is_object())
			{
				if (const nlohmann::json::const_iterator nestedInputSchema = nested->find("input_schema");
					nestedInputSchema != nested->end())
				{
					return &*nestedInputSchema;
				}
			}

			const nlohmann::json::const_iterator inputSchema = tool.find("input_schema");
			return inputSchema != tool.end() ? &*inputSchema : nullptr;
		}

		std::string ToolSchemasText(const nlohmann::json& snapshot, std::size_t& count)
		{
			std::vector<std::string> tools;
			const nlohmann::json::const_iterator found = snapshot.find("tools");

			if (found != snapshot.end() && found->is_array())
			{
				for (const nlohmann::json& tool : *found)
				{
					if (!tool.is_object())
					{
						continue;
					}

					std::vector<std::string> parts{StringAt(tool, "name")};

					if (std::string description = StringAt(tool, "description"); !description.empty())
					{
						parts.push_back(std::move(description));
					}

					if (const nlohmann::json* schema = InputSchemaOf(tool))
					{
						parts.push_back(schema->dump(kToolSchemaJsonIndent, kJsonIndentCharacter, kJsonEnsureAscii, kJsonInvalidUtf8Handler));
					}

					tools.push_back(Core::JoinedWith(parts, "\n\n"));
				}
			}

			count = tools.size();
			return Core::JoinedWith(tools, "\n\n" + std::string(kToolRuleLength, '-') + "\n\n");
		}

		using ClaudeCodeVersion = std::array<unsigned, 3>;

		constexpr ClaudeCodeVersion kFirstVersionRecordingRenderedText{2, 1, 260};

		std::optional<ClaudeCodeVersion> ParseVersion(const std::string& text)
		{
			ClaudeCodeVersion parts{};
			const char* cursor = text.data();
			const char* textEnd = text.data() + text.size();

			for (std::size_t partIndex = 0; partIndex < parts.size(); ++partIndex)
			{
				if (partIndex > 0)
				{
					if (cursor == textEnd || *cursor != '.')
					{
						return std::nullopt;
					}
					++cursor;
				}

				const auto [numberEnd, conversionError] = std::from_chars(cursor, textEnd, parts[partIndex]);

				if (conversionError != std::errc{})
				{
					return std::nullopt;
				}

				cursor = numberEnd;
			}

			return parts;
		}

		TokenCounts ParseUsage(const nlohmann::json& usage)
		{
			TokenCounts counts;
			counts.Input = CountAt(usage, "input_tokens");
			counts.Output = CountAt(usage, "output_tokens");
			counts.CacheRead = CountAt(usage, "cache_read_input_tokens");

			const std::uint64_t totalCacheWritten = CountAt(usage, "cache_creation_input_tokens");

			if (const nlohmann::json::const_iterator split = usage.find("cache_creation");
				split != usage.end() && split->is_object())
			{
				counts.CacheWrite5m = CountAt(*split, "ephemeral_5m_input_tokens");
				counts.CacheWrite1h = CountAt(*split, "ephemeral_1h_input_tokens");
			}

			const std::uint64_t cacheWrittenInSplit = counts.CacheWrite5m + counts.CacheWrite1h;

			if (cacheWrittenInSplit < totalCacheWritten)
			{
				counts.CacheWrite5m += totalCacheWritten - cacheWrittenInSplit;
			}

			return counts;
		}
	}

	std::optional<TranscriptRecord> ParseRecord(std::string_view line)
	{
		const nlohmann::json::parser_callback_t noParseCallback = nullptr;
		const nlohmann::json lineJson = nlohmann::json::parse(line, noParseCallback, kParseWithoutThrowing);

		if (lineJson.is_discarded() || !lineJson.is_object())
		{
			return std::nullopt;
		}

		TranscriptRecord record;
		record.TypeName = StringAt(lineJson, "type");
		record.Kind = RecordKindOf(record.TypeName);
		record.Uuid = StringAt(lineJson, "uuid");
		record.Timestamp = StringAt(lineJson, "timestamp");
		record.Cwd = StringAt(lineJson, "cwd");
		record.RequestId = StringAt(lineJson, "requestId");
		record.IsCompactSummary = BoolAt(lineJson, "isCompactSummary");
		record.IsMeta = BoolAt(lineJson, "isMeta") || record.IsCompactSummary;

		if (record.Kind == RecordKind::System && StringAt(lineJson, "subtype") == "compact_boundary")
		{
			record.IsCompactBoundary = true;
			record.Compaction = CompactionOf(lineJson);
		}

		if (record.Kind == RecordKind::AiTitle)
		{
			record.Title = StringAt(lineJson, "aiTitle");
		}

		if (const nlohmann::json::const_iterator attachment = lineJson.find("attachment");
			attachment != lineJson.end() && attachment->is_object())
		{
			record.AttachmentType = StringAt(*attachment, "type");

			const bool queued = record.AttachmentType == "queued_command";

			if (std::optional<std::string> rendered = RenderedText(lineJson))
			{
				record.AttachmentText = std::move(*rendered);
			}
			else if (const std::optional<ClaudeCodeVersion> version = ParseVersion(StringAt(lineJson, "version"));
					 !queued && version.has_value() && *version >= kFirstVersionRecordingRenderedText)
			{
				record.AttachmentSent = false;
			}
			else
			{
				std::optional<std::string> prompt = queued ? PromptText(*attachment) : std::nullopt;
				record.AttachmentText = prompt ? std::move(*prompt) : ReconstructedText(*attachment);
				record.AttachmentApproximate = true;
			}

			if (record.AttachmentType == "deferred_tools_delta")
			{
				record.AttachmentNames = StringsAt(*attachment, "addedNames");
				record.AttachmentRemoved = StringsAt(*attachment, "removedNames");
			}
			else if (record.AttachmentType == "mcp_instructions_delta")
			{
				record.AttachmentNames = StringsAt(*attachment, "addedNames");
			}
			else if (record.AttachmentType == "skill_listing")
			{
				record.AttachmentNames = StringsAt(*attachment, "names");
			}
			else if (record.AttachmentType == "edited_text_file")
			{
				record.AttachmentFile = StringAt(*attachment, "filename");
			}
			else if (record.AttachmentType == "prompt_snapshot")
			{
				record.SystemPromptText = SystemPromptText(*attachment);
				record.ToolSchemasText = ToolSchemasText(*attachment, record.ToolSchemaCount);
			}

			if (queued)
			{
				const nlohmann::json::const_iterator origin = attachment->find("origin");
				const bool typedByHuman = origin != attachment->end() && origin->is_object() &&
										  StringAt(*origin, "kind") == "human";

				const std::string commandMode = StringAt(*attachment, "commandMode");

				if (typedByHuman && (commandMode.empty() || commandMode == "prompt"))
				{
					record.QueuedPrompt = PromptText(*attachment).value_or(std::string{});
				}
			}
		}

		if (const nlohmann::json::const_iterator message = lineJson.find("message");
			message != lineJson.end() && message->is_object())
		{
			record.Model = StringAt(*message, "model");
			record.StopReason = StringAt(*message, "stop_reason");

			if (const nlohmann::json::const_iterator content = message->find("content"); content != message->end())
			{
				if (content->is_string())
				{
					record.Blocks.push_back({.Kind = BlockKind::Text, .Text = content->get<std::string>()});
				}
				else if (content->is_array())
				{
					for (const nlohmann::json& block : *content)
					{
						record.Blocks.push_back(ParseBlock(block));
					}
				}
			}

			if (const nlohmann::json::const_iterator usage = message->find("usage");
				usage != message->end() && usage->is_object())
			{
				record.Usage = ParseUsage(*usage);
				record.Fast = StringAt(*usage, "speed") == "fast";
			}
		}

		if (const nlohmann::json::const_iterator toolUseResult = lineJson.find("toolUseResult");
			toolUseResult != lineJson.end() && toolUseResult->is_object())
		{
			const std::string serialisedResult = Serialised(*toolUseResult);

			for (ContentBlock& block : record.Blocks)
			{
				if (block.Kind == BlockKind::ToolResult)
				{
					block.ResultJson = serialisedResult;
				}
			}
		}

		record.Bytes = line.size();

		return record;
	}

	TranscriptChunk ParseTranscript(std::string_view bytes)
	{
		TranscriptChunk chunk;
		std::size_t lineStart = 0;

		while (lineStart < bytes.size())
		{
			const std::size_t lineEnd = bytes.find('\n', lineStart);

			if (lineEnd == std::string_view::npos)
			{
				break;
			}

			std::string_view line = bytes.substr(lineStart, lineEnd - lineStart);

			if (!line.empty() && line.back() == '\r')
			{
				line.remove_suffix(1);
			}

			if (!line.empty())
			{
				if (std::optional<TranscriptRecord> record = ParseRecord(line))
				{
					chunk.Records.push_back(std::move(*record));
				}
				else
				{
					++chunk.Skipped;
				}
			}

			lineStart = lineEnd + 1;
		}

		chunk.Consumed = lineStart;
		return chunk;
	}

	Core::Result<TranscriptRead> ReadTranscriptChunk(const std::filesystem::path& file,
													 std::uint64_t offset, std::uint64_t maxBytes)
	{
		std::error_code sizeStatus;
		const std::uintmax_t size = std::filesystem::file_size(file, sizeStatus);

		if (sizeStatus)
		{
			return Core::Error{Core::ErrorCode::NotFound, "cannot read the transcript's size"};
		}

		TranscriptRead transcriptRead;
		transcriptRead.FileSize = size;

		if (size < offset)
		{
			transcriptRead.Restarted = true;
			offset = 0;
		}

		std::ifstream stream{file, std::ios::binary};

		if (!stream)
		{
			return Core::Error{Core::ErrorCode::IoFailure, "cannot open the transcript"};
		}

		stream.seekg(static_cast<std::streamoff>(offset));

		const std::uint64_t available = size - offset;
		const std::uint64_t readStep = std::max<std::uint64_t>(maxBytes, kMinimumReadStep);
		std::string bytes;

		while (bytes.size() < available)
		{
			const std::size_t sizeBeforeRead = bytes.size();
			const std::uint64_t bytesToRead = std::min<std::uint64_t>(readStep, available - sizeBeforeRead);

			bytes.resize(sizeBeforeRead + static_cast<std::size_t>(bytesToRead));
			stream.read(bytes.data() + sizeBeforeRead, static_cast<std::streamsize>(bytesToRead));
			bytes.resize(sizeBeforeRead + static_cast<std::size_t>(stream.gcount()));

			if (bytes.size() == sizeBeforeRead || bytes.find('\n', sizeBeforeRead) != std::string::npos)
			{
				break;
			}
		}

		transcriptRead.Chunk = ParseTranscript(bytes);
		transcriptRead.NextOffset = offset + transcriptRead.Chunk.Consumed;
		return transcriptRead;
	}

	Core::Result<TranscriptRead> ReadTranscript(const std::filesystem::path& file,
												std::uint64_t offset)
	{
		return ReadTranscriptChunk(file, offset, UINT64_MAX);
	}
}
