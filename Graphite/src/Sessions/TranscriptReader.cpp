#include "Sessions/TranscriptReader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

		// The string elements of an array field: empty for a missing or non-array field,
		// skipping any element that is not itself a string.
		std::vector<std::string> StringsAt(const nlohmann::json& object, const char* key)
		{
			std::vector<std::string> values;
			const auto found = object.find(key);

			if (found == object.end() || !found->is_array())
				return values;

			for (const nlohmann::json& element : *found)
			{
				if (element.is_string())
					values.push_back(element.get<std::string>());
			}

			return values;
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
			else if (type == "image")
			{
				// Only what the views say about it: its type and how much of it there is. A
				// source that is missing or of another shape leaves both unknown.
				parsed.Kind = BlockKind::Image;

				if (const auto source = block.find("source"); source != block.end() && source->is_object())
				{
					parsed.Text = StringAt(*source, "media_type");

					if (const auto data = source->find("data"); data != source->end() && data->is_string())
						parsed.DataBytes = data->get_ref<const std::string&>().size();
				}
			}
			else
			{
				parsed.Kind = BlockKind::Other;
				parsed.Text = type;
			}

			return parsed;
		}

		// A field that holds text for the model: a string, or a list whose strings are read a
		// line apiece. Nothing for a missing field or one of another type.
		std::optional<std::string> TextAt(const nlohmann::json& object, const char* key)
		{
			const auto found = object.find(key);

			if (found == object.end())
				return std::nullopt;
			if (found->is_string())
				return found->get<std::string>();
			if (!found->is_array())
				return std::nullopt;

			std::string text;
			bool first = true;

			for (const nlohmann::json& element : *found)
			{
				if (!first)
					text += '\n';
				first = false;

				text += element.is_string() ? element.get<std::string>() : Serialised(element);
			}

			return text;
		}

		// What an attachment written before `rendered` existed most likely put in front of the
		// model. The fields are the ones Claude Code wrote the model's text into; an attachment
		// with none of them is shown as itself, compactly, which is closer to what it cost
		// than the record around it.
		std::string ReconstructedText(const nlohmann::json& attachment)
		{
			for (const char* key : {"text", "content", "snippet", "prompt", "addedBlocks", "addedLines"})
			{
				if (std::optional<std::string> text = TextAt(attachment, key))
					return std::move(*text);
			}

			return Serialised(attachment);
		}

		// The text an attachment sent, as Claude Code recorded it beside the attachment: one
		// element per text block, joined a block to a line. Nothing when the record has no
		// `rendered` list. An element of another shape contributes nothing.
		std::optional<std::string> RenderedText(const nlohmann::json& record)
		{
			const auto found = record.find("rendered");

			if (found == record.end() || !found->is_array())
				return std::nullopt;

			std::string text;
			bool first = true;

			for (const nlohmann::json& element : *found)
			{
				if (!element.is_object())
					continue;

				const auto content = element.find("content");

				if (content == element.end() || !content->is_string())
					continue;

				if (!first)
					text += '\n';
				first = false;

				text += content->get_ref<const std::string&>();
			}

			return text;
		}

		std::string JoinedWith(const std::vector<std::string>& parts, std::string_view separator)
		{
			std::string joined;

			for (std::size_t i = 0; i < parts.size(); ++i)
			{
				if (i > 0)
					joined += separator;
				joined += parts[i];
			}

			return joined;
		}

		// The CLI prefix and the system prompt's blocks, a blank line between. A snapshot with
		// no blocks has no system prompt, whatever prefix it carries.
		std::string SystemPromptText(const nlohmann::json& snapshot)
		{
			std::vector<std::string> blocks = StringsAt(snapshot, "systemPrompt");

			if (blocks.empty())
				return {};

			if (std::string prefix = StringAt(snapshot, "cliPrefix"); !prefix.empty())
				blocks.insert(blocks.begin(), std::move(prefix));

			return JoinedWith(blocks, "\n\n");
		}

		// Each tool as the model reads its definition: name, description, and input schema
		// laid out two spaces an indent, the parts a blank line apart and a rule between
		// tools. An element that is not an object is not a tool.
		std::string ToolSchemasText(const nlohmann::json& snapshot, std::size_t& count)
		{
			std::vector<std::string> tools;
			const auto found = snapshot.find("tools");

			if (found != snapshot.end() && found->is_array())
			{
				for (const nlohmann::json& tool : *found)
				{
					if (!tool.is_object())
						continue;

					std::vector<std::string> parts{StringAt(tool, "name")};

					if (std::string description = StringAt(tool, "description"); !description.empty())
						parts.push_back(std::move(description));

					if (const auto schema = tool.find("input_schema"); schema != tool.end())
						parts.push_back(schema->dump(2, ' ', false, nlohmann::json::error_handler_t::replace));

					tools.push_back(JoinedWith(parts, "\n\n"));
				}
			}

			count = tools.size();
			return JoinedWith(tools, "\n\n" + std::string(40, '-') + "\n\n");
		}

		using Version = std::array<unsigned, 3>;

		// The first Claude Code that records what an attachment sent. From it on, an
		// attachment with no record of that was not sent at all.
		constexpr Version kRenderedSince{2, 1, 260};

		// "2.1.280" as its three numbers; whatever follows the third is ignored. Nothing when
		// the text does not start with three dot-separated numbers.
		std::optional<Version> ParseVersion(const std::string& text)
		{
			Version parts{};
			const char* at = text.data();
			const char* end = text.data() + text.size();

			for (std::size_t i = 0; i < parts.size(); ++i)
			{
				if (i > 0)
				{
					if (at == end || *at != '.')
						return std::nullopt;
					++at;
				}

				const auto [next, error] = std::from_chars(at, end, parts[i]);

				if (error != std::errc{})
					return std::nullopt;

				at = next;
			}

			return parts;
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

			if (std::optional<std::string> rendered = RenderedText(json))
			{
				record.AttachmentText = std::move(*rendered);
			}
			else if (const std::optional<Version> version = ParseVersion(StringAt(json, "version"));
					 version.has_value() && *version >= kRenderedSince)
			{
				record.AttachmentSent = false;
			}
			else
			{
				// Older than `rendered`, or silent about its version: showing a reconstruction
				// is better than hiding something that may well have been sent.
				record.AttachmentText = ReconstructedText(*attachment);
				record.AttachmentApproximate = true;
			}

			if (record.AttachmentType == "deferred_tools_delta")
			{
				record.AttachmentNames = StringsAt(*attachment, "addedNames");
				record.AttachmentRemoved = StringsAt(*attachment, "removedNames");
			}
			else if (record.AttachmentType == "mcp_instructions_delta")
				record.AttachmentNames = StringsAt(*attachment, "addedNames");
			else if (record.AttachmentType == "skill_listing")
				record.AttachmentNames = StringsAt(*attachment, "names");
			else if (record.AttachmentType == "edited_text_file")
				record.AttachmentFile = StringAt(*attachment, "filename");
			else if (record.AttachmentType == "prompt_snapshot")
			{
				record.SystemPromptText = SystemPromptText(*attachment);
				record.ToolSchemasText = ToolSchemasText(*attachment, record.ToolSchemaCount);
			}

			if (record.AttachmentType == "queued_command")
			{
				const auto origin = attachment->find("origin");
				const bool human = origin != attachment->end() && origin->is_object() &&
								   StringAt(*origin, "kind") == "human";

				// Other modes (a queued shell command) are not something the user said to Claude.
				const std::string mode = StringAt(*attachment, "commandMode");

				if (human && (mode.empty() || mode == "prompt"))
					record.QueuedPrompt = StringAt(*attachment, "prompt");
			}
		}

		if (const auto message = json.find("message"); message != json.end() && message->is_object())
		{
			record.Model = StringAt(*message, "model");
			record.StopReason = StringAt(*message, "stop_reason");

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

		record.Bytes = line.size();

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

	Core::Result<TranscriptRead> ReadTranscriptChunk(const std::filesystem::path& file,
													 std::uint64_t offset, std::uint64_t maxBytes)
	{
		std::error_code status;
		const std::uintmax_t size = std::filesystem::file_size(file, status);

		if (status)
			return Core::Error{Core::ErrorCode::NotFound, "cannot read the transcript's size"};

		TranscriptRead read;
		read.FileSize = size;

		if (size < offset)
		{
			read.Restarted = true;
			offset = 0;
		}

		std::ifstream stream{file, std::ios::binary};

		if (!stream)
			return Core::Error{Core::ErrorCode::IoFailure, "cannot open the transcript"};

		stream.seekg(static_cast<std::streamoff>(offset));

		const std::uint64_t available = size - offset;
		const std::uint64_t step = std::max<std::uint64_t>(maxBytes, 1);
		std::string bytes;

		// Grown a step at a time until the bytes hold a whole line or the file runs out.
		while (bytes.size() < available)
		{
			const std::size_t had = bytes.size();
			const std::uint64_t wanted = std::min<std::uint64_t>(step, available - had);

			bytes.resize(had + static_cast<std::size_t>(wanted));
			stream.read(bytes.data() + had, static_cast<std::streamsize>(wanted));
			bytes.resize(had + static_cast<std::size_t>(stream.gcount()));

			if (bytes.size() == had || bytes.find('\n', had) != std::string::npos)
				break;
		}

		read.Chunk = ParseTranscript(bytes);
		read.NextOffset = offset + read.Chunk.Consumed;
		return read;
	}

	Core::Result<TranscriptRead> ReadTranscript(const std::filesystem::path& file,
												std::uint64_t offset)
	{
		return ReadTranscriptChunk(file, offset, UINT64_MAX);
	}
}
