#include "Sessions/TranscriptReader.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

namespace Graphite {

	namespace {

		// Every accessor below answers a wrong type with a default instead of throwing: a
		// field whose type changed in a newer Claude Code must cost that field, not the line.
		std::string StringAt(const nlohmann::json& object, const char* key)
		{
			const auto found = object.find(key);
			return found != object.end() && found->is_string() ? found->get<std::string>()
															   : std::string{};
		}

		bool BoolAt(const nlohmann::json& object, const char* key)
		{
			const auto found = object.find(key);
			return found != object.end() && found->is_boolean() && found->get<bool>();
		}

		std::uint64_t CountAt(const nlohmann::json& object, const char* key)
		{
			const auto found = object.find(key);

			if (found == object.end())
				return 0;
			if (found->is_number_unsigned())
				return found->get<std::uint64_t>();
			if (found->is_number_integer() && found->get<std::int64_t>() > 0)
				return static_cast<std::uint64_t>(found->get<std::int64_t>());

			return 0;
		}

		RecordKind KindOf(const std::string& type)
		{
			if (type == "user")
				return RecordKind::User;
			if (type == "assistant")
				return RecordKind::Assistant;
			if (type == "attachment")
				return RecordKind::Attachment;
			if (type == "system")
				return RecordKind::System;
			if (type == "ai-title")
				return RecordKind::AiTitle;

			return RecordKind::Other;
		}

		std::string Serialised(const nlohmann::json& value)
		{
			// `replace` rather than the default `strict`: a tool input holding invalid UTF-8
			// must not throw out of a reader that promises not to.
			return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
		}

		// A tool result's content is a string or a list of blocks, of which the text ones
		// are what a reader wants.
		std::string ResultText(const nlohmann::json& content)
		{
			if (content.is_string())
				return content.get<std::string>();

			std::string joined;

			if (content.is_array())
			{
				for (const nlohmann::json& part : content)
				{
					if (part.is_object() && StringAt(part, "type") == "text")
					{
						if (!joined.empty())
							joined += '\n';
						joined += StringAt(part, "text");
					}
				}
			}

			return joined;
		}

		ContentBlock ParseBlock(const nlohmann::json& block)
		{
			ContentBlock parsed;

			if (!block.is_object())
				return parsed;

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

				if (const auto input = block.find("input"); input != block.end())
					parsed.InputJson = Serialised(*input);
			}
			else if (type == "tool_result")
			{
				parsed.Kind = BlockKind::ToolResult;
				parsed.ToolUseId = StringAt(block, "tool_use_id");
				parsed.IsError = BoolAt(block, "is_error");

				if (const auto content = block.find("content"); content != block.end())
					parsed.Text = ResultText(*content);
			}
			else
			{
				parsed.Kind = BlockKind::Other;
				parsed.Text = type;
			}

			return parsed;
		}

		TokenCounts ParseUsage(const nlohmann::json& usage)
		{
			TokenCounts counts;
			counts.Input = CountAt(usage, "input_tokens");
			counts.Output = CountAt(usage, "output_tokens");
			counts.CacheRead = CountAt(usage, "cache_read_input_tokens");

			const std::uint64_t written = CountAt(usage, "cache_creation_input_tokens");

			if (const auto split = usage.find("cache_creation");
				split != usage.end() && split->is_object())
			{
				counts.CacheWrite5m = CountAt(*split, "ephemeral_5m_input_tokens");
				counts.CacheWrite1h = CountAt(*split, "ephemeral_1h_input_tokens");
			}

			// Older records have the total and no split. Whatever the split does not account
			// for is priced as the cheaper, five-minute write — the default lifetime.
			const std::uint64_t accounted = counts.CacheWrite5m + counts.CacheWrite1h;

			if (accounted < written)
				counts.CacheWrite5m += written - accounted;

			return counts;
		}
	}

	std::optional<TranscriptRecord> ParseRecord(std::string_view line)
	{
		const nlohmann::json json = nlohmann::json::parse(line, nullptr, false);

		if (json.is_discarded() || !json.is_object())
			return std::nullopt;

		TranscriptRecord record;
		record.TypeName = StringAt(json, "type");
		record.Kind = KindOf(record.TypeName);
		record.Uuid = StringAt(json, "uuid");
		record.Timestamp = StringAt(json, "timestamp");
		record.Cwd = StringAt(json, "cwd");
		record.RequestId = StringAt(json, "requestId");
		record.IsMeta = BoolAt(json, "isMeta");

		if (record.Kind == RecordKind::AiTitle)
			record.Title = StringAt(json, "aiTitle");

		if (const auto attachment = json.find("attachment");
			attachment != json.end() && attachment->is_object())
		{
			record.AttachmentType = StringAt(*attachment, "type");
		}

		if (const auto message = json.find("message"); message != json.end() && message->is_object())
		{
			record.Model = StringAt(*message, "model");

			if (const auto content = message->find("content"); content != message->end())
			{
				if (content->is_string())
				{
					record.Blocks.push_back({.Kind = BlockKind::Text, .Text = content->get<std::string>()});
				}
				else if (content->is_array())
				{
					for (const nlohmann::json& block : *content)
						record.Blocks.push_back(ParseBlock(block));
				}
			}

			if (const auto usage = message->find("usage"); usage != message->end() && usage->is_object())
			{
				record.Usage = ParseUsage(*usage);
				record.Fast = StringAt(*usage, "speed") == "fast";
			}
		}

		// Claude Code writes the structured result once per record, beside the message rather
		// than inside the block; a record answers one tool, so every result block gets it.
		if (const auto result = json.find("toolUseResult"); result != json.end() && result->is_object())
		{
			const std::string serialised = Serialised(*result);

			for (ContentBlock& block : record.Blocks)
			{
				if (block.Kind == BlockKind::ToolResult)
					block.ResultJson = serialised;
			}
		}

		return record;
	}

	TranscriptChunk ParseTranscript(std::string_view bytes)
	{
		TranscriptChunk chunk;
		std::size_t start = 0;

		while (start < bytes.size())
		{
			const std::size_t end = bytes.find('\n', start);

			if (end == std::string_view::npos)
				break;

			std::string_view line = bytes.substr(start, end - start);

			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);

			if (!line.empty())
			{
				if (std::optional<TranscriptRecord> record = ParseRecord(line))
					chunk.Records.push_back(std::move(*record));
				else
					++chunk.Skipped;
			}

			start = end + 1;
		}

		chunk.Consumed = start;
		return chunk;
	}

	Core::Result<TranscriptRead> ReadTranscript(const std::filesystem::path& file,
												std::uint64_t offset)
	{
		std::error_code status;
		const std::uintmax_t size = std::filesystem::file_size(file, status);

		if (status)
			return Core::Error{Core::ErrorCode::NotFound, "cannot read the transcript's size"};

		TranscriptRead read;

		if (size < offset)
		{
			read.Restarted = true;
			offset = 0;
		}

		std::ifstream stream{file, std::ios::binary};

		if (!stream)
			return Core::Error{Core::ErrorCode::IoFailure, "cannot open the transcript"};

		stream.seekg(static_cast<std::streamoff>(offset));

		std::string bytes(static_cast<std::size_t>(size - offset), '\0');
		stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		bytes.resize(static_cast<std::size_t>(stream.gcount()));

		read.Chunk = ParseTranscript(bytes);
		read.NextOffset = offset + read.Chunk.Consumed;
		return read;
	}
}
