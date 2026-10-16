#include "Sessions/Requests/RequestTimeline.h"

#include "Core/Base/ByteSize.h"
#include "Core/Base/Strings.h"
#include "Core/Text/Encoding.h"

#include "Sessions/Transcript/InjectedText.h"
#include "Sessions/Requests/RequestUsageAccumulator.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "Sessions/Transcript/ToolFacts.h"
#include "UI/TypographicMarks.h"

#include <algorithm>
#include <format>
#include <span>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace Graphite {

	namespace {

		constexpr std::size_t kMaxNamesShownInToolsLabel = 5;
		constexpr std::size_t kLabelFirstLineMaxBytes = 120;
		constexpr std::string_view kImageMediaTypePrefix = "image/";
		constexpr std::size_t kBase64EncodedGroupBytes = 4;
		constexpr std::size_t kBase64DecodedGroupBytes = 3;
		constexpr std::uint64_t kCacheWriteFractionDenominator = 2;
		constexpr std::size_t kNoToolsLoaded = 0;
		constexpr int kJsonIndentWidth = 2;

		using Timestamp = std::chrono::sys_time<std::chrono::milliseconds>;

		std::string JoinNames(const std::vector<std::string>& names, std::size_t maxNames)
		{
			const std::size_t shownCount = std::min(names.size(), maxNames);
			std::string joined = Core::JoinedWith(std::span<const std::string>(names.data(), shownCount), ", ");

			if (names.size() > maxNames)
			{
				joined += ", ";
				joined += kEllipsis;
			}

			return joined;
		}

		std::string ToolCountLabel(std::size_t count)
		{
			return Core::Counted(count, "tool");
		}

		std::string DeferredNamesLabel(std::size_t count)
		{
			return Core::Counted(count, "deferred tool name");
		}

		std::string ToolsLoadedLabel(const std::vector<std::string>& added, const std::vector<std::string>& removed)
		{
			if (added.empty() && removed.empty())
			{
				return "tools unchanged";
			}

			if (added.empty())
			{
				return std::string{kMinusSign} + ToolCountLabel(removed.size()) + " removed";
			}

			std::string label = DeferredNamesLabel(added.size()) + ": " + JoinNames(added, kMaxNamesShownInToolsLabel);

			if (!removed.empty())
			{
				label += ", " + std::string{kMinusSign} + std::to_string(removed.size()) + " removed";
			}

			return label;
		}

		std::string ImageLabel(std::string_view mediaType, std::size_t byteCount)
		{
			std::vector<std::string> parts;

			if (!mediaType.empty())
			{
				parts.emplace_back(mediaType.starts_with(kImageMediaTypePrefix) ? mediaType.substr(kImageMediaTypePrefix.size())
																				: mediaType);
			}
			if (byteCount > 0)
			{
				parts.push_back(Core::FormatByteSize(byteCount));
			}

			return parts.empty() ? std::string{"Image"} : "Image (" + Core::JoinedWith(parts, ", ") + ")";
		}

		std::string PrettyJson(const std::string& json)
		{
			const nlohmann::json::parser_callback_t noCallback = nullptr;
			const bool allowExceptions = false;
			const nlohmann::json parsed = nlohmann::json::parse(json, noCallback, allowExceptions);

			const char indentCharacter = ' ';
			const bool ensureAscii = false;

			return parsed.is_discarded()
					   ? json
					   : parsed.dump(kJsonIndentWidth, indentCharacter, ensureAscii, nlohmann::json::error_handler_t::replace);
		}

		std::optional<std::chrono::milliseconds> WaitBetween(const std::string& sentAt, const std::string& finishedAt)
		{
			const std::optional<Timestamp> sentTime = ParseTimestamp(sentAt);
			const std::optional<Timestamp> finishedTime = ParseTimestamp(finishedAt);

			if (!sentTime.has_value() || !finishedTime.has_value() || *finishedTime < *sentTime)
			{
				return std::nullopt;
			}

			return *finishedTime - *sentTime;
		}

		std::string McpLabel(const std::vector<std::string>& names)
		{
			return "MCP: " + Core::JoinedWith(names, ", ");
		}

		std::string ThinkingLabel(const std::string& text)
		{
			return text.empty() ? "thinking (empty)" : "thinking (" + std::to_string(Core::CharacterCount(text)) + " chars)";
		}

		std::string InjectedTextLabel(const TranscriptRecord& record, const std::string& text)
		{
			if (record.IsCompactSummary)
			{
				return std::string{kCompactSummaryLabel};
			}

			if (IsInjectedText(text))
			{
				return InjectedLabel(text);
			}

			return FirstLineLabel(text, kLabelFirstLineMaxBytes);
		}

		bool IsSnapshotItem(const SentItem& item)
		{
			return item.Kind == SentKind::SystemPrompt || item.Kind == SentKind::ToolSchemas;
		}

		bool IsNewToolName(const SentItem& item, const std::unordered_set<std::string>& seenNames)
		{
			return item.Kind == SentKind::ToolResult && !item.ToolName.empty() && !seenNames.contains(item.ToolName);
		}
	}

	void RequestTimeline::ApplyUsage(ApiRequest& request, std::size_t index, const TokenCounts& usage)
	{
		request.Usage = usage;
		request.Context =
			request.Usage.Input + request.Usage.CacheWrite5m + request.Usage.CacheWrite1h + request.Usage.CacheRead;
		request.ContextDelta = ContextDeltaFor(index);
		request.CachedShare = request.Context > 0
								  ? static_cast<double>(request.Usage.CacheRead) / static_cast<double>(request.Context)
								  : 0.0;

		const std::uint64_t cacheWrites = request.Usage.CacheWrite5m + request.Usage.CacheWrite1h;
		request.CacheRebuilt = request.Context > 0 && cacheWrites * kCacheWriteFractionDenominator >= request.Context;
	}

	std::optional<std::int64_t> RequestTimeline::ContextDeltaFor(std::size_t index) const
	{
		// WARNING: A request with no context (no usage, or the all-zero usage of a response Claude Code
		// wrote itself for an interrupt or an API error) is neither measured nor a baseline: its zero
		// would read as a drop, and the next request as adding its whole context.
		const bool hasNoContext = m_Requests[index].Context == 0;

		if (hasNoContext)
		{
			return std::nullopt;
		}

		for (std::size_t earlierIndex = index; earlierIndex-- > 0;)
		{
			if (m_HasUsage[earlierIndex] && m_Requests[earlierIndex].Context > 0)
			{
				return static_cast<std::int64_t>(m_Requests[index].Context) -
					   static_cast<std::int64_t>(m_Requests[earlierIndex].Context);
			}
		}

		return std::nullopt;
	}

	void RequestTimeline::AddAssistant(const TranscriptRecord& record, bool& changed)
	{
		const std::string& key = record.RequestId.empty() ? record.Uuid : record.RequestId;

		// WARNING: An empty key means Claude Code gave this response neither a requestId nor a uuid.
		// Such responses are never deduplicated: they are not the same request just because they share
		// the one key every keyless record gets.
		const bool keyed = !key.empty();
		const std::optional<std::size_t> existingIndex = keyed ? IndexOf(key) : std::nullopt;
		ApiRequest* request = nullptr;
		std::size_t index = 0;

		if (!existingIndex.has_value())
		{
			ApiRequest newRequest;
			newRequest.Index = m_Requests.size() + 1;
			newRequest.Key = key;
			newRequest.Model = record.Model;
			newRequest.Fast = record.Fast;
			newRequest.RespondedAt = record.Timestamp;
			newRequest.SentAt = m_LastTimestamp;
			newRequest.FinishedAt = record.Timestamp;
			newRequest.Waited = WaitBetween(newRequest.SentAt, newRequest.FinishedAt);

			newRequest.Sent = std::move(m_Pending);
			m_Pending.clear();
			newRequest.ToolsAvailable = m_Tools.size();
			newRequest.ToolsLoaded = std::exchange(m_PendingLoaded, kNoToolsLoaded);
			newRequest.ToolSchemas = m_SnapshotToolSchemas;

			if (m_Requests.empty())
			{
				PlaceSnapshot(newRequest);
			}

			const bool hasUsage = record.Usage.has_value();

			m_Requests.push_back(std::move(newRequest));
			m_HasUsage.push_back(hasUsage);
			index = m_Requests.size() - 1;
			request = &m_Requests[index];

			if (hasUsage)
			{
				ApplyUsage(*request, index, *record.Usage);
			}

			if (keyed)
			{
				m_ByKey[key] = index;
			}

			changed = true;
		}
		else
		{
			index = *existingIndex;
			request = &m_Requests[index];

			const std::optional<Timestamp> recordTime = ParseTimestamp(record.Timestamp);
			const std::optional<Timestamp> finishedTime = ParseTimestamp(request->FinishedAt);

			if (recordTime.has_value() && (!finishedTime.has_value() || *recordTime > *finishedTime))
			{
				request->FinishedAt = record.Timestamp;
				request->Waited = WaitBetween(request->SentAt, request->FinishedAt);
				changed = true;
			}

			if (record.Usage.has_value())
			{
				RequestUsageAccumulator accumulator =
					m_HasUsage[index] ? RequestUsageAccumulator{request->Usage} : RequestUsageAccumulator{};

				if (accumulator.Add(*record.Usage))
				{
					const std::uint64_t contextBefore = request->Context;
					ApplyUsage(*request, index, accumulator.GetCounts());
					m_HasUsage[index] = true;
					changed = true;

					if (request->Context != contextBefore)
					{
						for (std::size_t laterIndex = index + 1; laterIndex < m_Requests.size(); ++laterIndex)
						{
							if (m_HasUsage[laterIndex] && m_Requests[laterIndex].Context > 0)
							{
								m_Requests[laterIndex].ContextDelta = ContextDeltaFor(laterIndex);
								break;
							}
						}
					}
				}
			}
		}

		// WARNING: A response written as several records repeats its stop reason on the last one; a
		// blank one on an earlier record must not erase it.
		if (!record.StopReason.empty() && request->StopReason != record.StopReason)
		{
			request->StopReason = record.StopReason;
			changed = true;
		}

		for (const ContentBlock& block : record.Blocks)
		{
			switch (block.Kind)
			{
			case BlockKind::Thinking:
				request->Response.push_back(
					{.Kind = ResponseKind::Thinking, .Label = ThinkingLabel(block.Text), .Text = block.Text});
				changed = true;
				break;
			case BlockKind::Text:
				request->Response.push_back({.Kind = ResponseKind::Text,
											 .Label = std::to_string(Core::CharacterCount(block.Text)) + " chars",
											 .Text = block.Text});
				changed = true;
				break;
			case BlockKind::ToolUse:
			{
				std::string summary = ToolSummary({.Name = block.ToolName, .InputJson = block.InputJson});
				request->Response.push_back({.Kind = ResponseKind::ToolCall,
											 .Label = block.ToolName + "  " + summary,
											 .ToolName = block.ToolName,
											 .Text = block.ToolName + "\n" + PrettyJson(block.InputJson)});
				m_ToolCalls[block.ToolUseId] = {block.ToolName, std::move(summary)};
				changed = true;
				break;
			}
			case BlockKind::ToolResult:
			case BlockKind::Image:
				break;
			case BlockKind::Other:
				request->Response.push_back({.Kind = ResponseKind::Other, .Label = block.Text});
				changed = true;
				break;
			}
		}
	}

	void RequestTimeline::AddUser(const TranscriptRecord& record, bool& changed)
	{
		for (const ContentBlock& block : record.Blocks)
		{
			if (block.Kind == BlockKind::ToolResult)
			{
				std::string label = "unknown tool";
				std::string toolName;

				if (m_ToolCalls.contains(block.ToolUseId))
				{
					const ToolCallInfo& call = m_ToolCalls.at(block.ToolUseId);
					toolName = call.Name;
					label = toolName + "  " + call.Summary;
				}

				// WARNING: Only the result's text is sent. The structured result beside it is Claude
				// Code's own record (an Edit's carries the whole file) and never reaches the model.
				m_Pending.push_back({.Kind = SentKind::ToolResult,
									 .Label = label,
									 .ToolName = toolName,
									 .IsError = block.IsError,
									 .Bytes = block.Text.size(),
									 .Text = block.Text});
				changed = true;
			}
			else if (block.Kind == BlockKind::Image)
			{
				const std::size_t decodedBytes = block.DataBytes * kBase64DecodedGroupBytes / kBase64EncodedGroupBytes;
				m_Pending.push_back({.Kind = SentKind::Image,
									 .Label = ImageLabel(block.Text, decodedBytes),
									 .Bytes = decodedBytes,
									 .Text = ImageText(block.Text, decodedBytes)});
				changed = true;
			}
			else if (block.Kind == BlockKind::Text)
			{
				if (record.IsMeta || IsInjectedText(block.Text))
				{
					const std::string label = InjectedTextLabel(record, block.Text);
					m_Pending.push_back(
						{.Kind = SentKind::Injected, .Label = label, .Bytes = block.Text.size(), .Text = block.Text});
				}
				else
				{
					m_Pending.push_back({.Kind = SentKind::Prompt,
										 .Label = FirstLineLabel(block.Text, kLabelFirstLineMaxBytes),
										 .Bytes = block.Text.size(),
										 .Text = block.Text});
				}

				changed = true;
			}
		}
	}

	void RequestTimeline::AddAttachment(const TranscriptRecord& record, bool& changed)
	{
		if (record.AttachmentType == "prompt_snapshot")
		{
			AddSnapshot(record, changed);
			return;
		}

		if (!record.AttachmentSent)
		{
			return;
		}

		SentItem item;
		item.Text = record.AttachmentText;
		item.Bytes = item.Text.size();
		item.Approximate = record.AttachmentApproximate;

		if (!record.QueuedPrompt.empty())
		{
			item.Kind = SentKind::QueuedPrompt;
			item.Label = FirstLineLabel(record.QueuedPrompt, kLabelFirstLineMaxBytes);
		}
		else if (record.AttachmentType == "deferred_tools_delta")
		{
			item.Kind = SentKind::ToolsLoaded;
			item.Label = ToolsLoadedLabel(record.AttachmentNames, record.AttachmentRemoved);

			m_PendingLoaded += record.AttachmentNames.size();

			for (const std::string& name : record.AttachmentNames)
			{
				m_Tools.insert(name);
			}
			for (const std::string& name : record.AttachmentRemoved)
			{
				m_Tools.erase(name);
			}
		}
		else if (record.AttachmentType == "skill_listing")
		{
			item.Kind = SentKind::Skills;
			item.Label = std::to_string(record.AttachmentNames.size()) + " skills";
		}
		else if (record.AttachmentType == "mcp_instructions_delta")
		{
			item.Kind = SentKind::McpInstructions;
			item.Label = McpLabel(record.AttachmentNames);
		}
		else if (record.AttachmentType == "edited_text_file")
		{
			item.Kind = SentKind::EditedFile;
			item.Label = "Edited " + record.AttachmentFile;
		}
		else
		{
			item.Kind = SentKind::Reminder;
			item.Label = record.AttachmentType;
		}

		m_Pending.push_back(std::move(item));
		changed = true;
	}

	void RequestTimeline::AddSnapshot(const TranscriptRecord& record, bool& changed)
	{
		if (record.SystemPromptText.empty())
		{
			return;
		}

		m_Snapshot.clear();
		m_Snapshot.push_back({.Kind = SentKind::SystemPrompt,
							  .Label = "System prompt",
							  .Bytes = record.SystemPromptText.size(),
							  .Text = record.SystemPromptText});

		if (record.ToolSchemaCount > 0)
		{
			const std::size_t schemaCount = record.ToolSchemaCount;
			m_Snapshot.push_back({.Kind = SentKind::ToolSchemas,
								  .Label = "Tools (" + Core::Counted(schemaCount, "schema") + ")",
								  .Bytes = record.ToolSchemasText.size(),
								  .Text = record.ToolSchemasText});
		}

		if (!m_Requests.empty())
		{
			PlaceSnapshot(m_Requests.front());
		}

		m_SnapshotToolSchemas = record.ToolSchemaCount;

		for (ApiRequest& request : m_Requests)
		{
			request.ToolSchemas = m_SnapshotToolSchemas;
		}

		changed = true;
	}

	void RequestTimeline::PlaceSnapshot(ApiRequest& request) const
	{
		std::erase_if(request.Sent, IsSnapshotItem);
		request.Sent.insert(request.Sent.begin(), m_Snapshot.begin(), m_Snapshot.end());
	}

	void RequestTimeline::Append(const std::vector<TranscriptRecord>& records)
	{
		bool changed = false;

		for (const TranscriptRecord& record : records)
		{
			if (record.Kind == RecordKind::Assistant)
			{
				AddAssistant(record, changed);
			}
			else if (record.Kind == RecordKind::User)
			{
				AddUser(record, changed);
			}
			else if (record.Kind == RecordKind::Attachment)
			{
				AddAttachment(record, changed);
			}

			// WARNING: A malformed timestamp is not "no timestamp". Keeping the last one that parsed
			// means the next request's Waited is still measured against real time.
			if (ParseTimestamp(record.Timestamp).has_value())
			{
				m_LastTimestamp = record.Timestamp;
			}
		}

		if (changed)
		{
			++m_Version;
		}
	}

	void RequestTimeline::Reset()
	{
		m_Requests.clear();
		m_ByKey.clear();
		m_HasUsage.clear();
		m_Pending.clear();
		m_Snapshot.clear();
		m_SnapshotToolSchemas = 0;
		m_ToolCalls.clear();
		m_Tools.clear();
		m_PendingLoaded = 0;
		m_LastTimestamp.clear();
		++m_Version;
	}

	std::optional<std::size_t> RequestTimeline::IndexOf(std::string_view key) const
	{
		if (!m_ByKey.contains(std::string(key)))
		{
			return std::nullopt;
		}

		return m_ByKey.at(std::string(key));
	}

	std::vector<ApiRequest> BuildRequestTimeline(const std::vector<TranscriptRecord>& records)
	{
		RequestTimeline timeline;
		timeline.Append(records);
		return timeline.GetRequests();
	}

	std::vector<std::string> ToolNamesUsed(const ApiRequest& request)
	{
		std::vector<std::string> names;
		std::unordered_set<std::string> seenNames;

		for (const SentItem& item : request.Sent)
		{
			if (IsNewToolName(item, seenNames))
			{
				seenNames.insert(item.ToolName);
				names.push_back(item.ToolName);
			}
		}

		for (const ResponseItem& item : request.Response)
		{
			if (item.Kind == ResponseKind::ToolCall && !item.ToolName.empty() && !seenNames.contains(item.ToolName))
			{
				seenNames.insert(item.ToolName);
				names.push_back(item.ToolName);
			}
		}

		return names;
	}

	std::string ToolsAvailableLabel(const ApiRequest& request)
	{
		std::vector<std::string> parts;

		if (request.ToolSchemas > 0)
		{
			parts.push_back(Core::Counted(request.ToolSchemas, "with schema"));
		}

		if (request.ToolsAvailable > 0)
		{
			parts.push_back(std::to_string(request.ToolsAvailable) + " by name only");
		}

		std::string label = "Tools: none recorded";

		if (!parts.empty())
		{
			label = "Tools: " + Core::JoinedWith(parts, ", ");
		}

		if (request.ToolsLoaded > 0)
		{
			label += " (+" + std::to_string(request.ToolsLoaded) + " new)";
		}

		return label;
	}

	std::string SentHeading(const ApiRequest& request)
	{
		return request.Sent.empty() ? "Nothing added to the context" : "Added to the context";
	}

	std::string SentSummary(const ApiRequest& request)
	{
		const std::string reference =
			request.Index == 1 ? "first request" : "since #" + std::to_string(request.Index - 1);

		if (request.Sent.empty())
		{
			return reference;
		}

		std::size_t totalBytes = 0;
		for (const SentItem& item : request.Sent)
		{
			totalBytes += item.Bytes;
		}

		return std::format("{}{}{}{}{}",
						   reference,
						   kMiddleDotSeparator,
						   Core::Counted(request.Sent.size(), "item"),
						   kMiddleDotSeparator,
						   Core::FormatByteSize(totalBytes));
	}

	std::string ReplyHeading(const ApiRequest& request)
	{
		return request.Response.empty() ? "Model's reply" + std::string{kEmDashSeparator} + "nothing yet" : "Model's reply";
	}

	std::string ReplySummary(const ApiRequest& request)
	{
		if (request.Response.empty())
		{
			return {};
		}

		std::size_t thinkingCount = 0;
		std::size_t textCount = 0;
		std::size_t toolCallCount = 0;
		std::size_t otherCount = 0;

		for (const ResponseItem& item : request.Response)
		{
			switch (item.Kind)
			{
			case ResponseKind::Thinking:
				++thinkingCount;
				break;
			case ResponseKind::Text:
				++textCount;
				break;
			case ResponseKind::ToolCall:
				++toolCallCount;
				break;
			case ResponseKind::Other:
				++otherCount;
				break;
			}
		}

		std::vector<std::string> parts;

		if (thinkingCount > 0)
		{
			parts.push_back(thinkingCount == 1 ? "thinking" : std::to_string(thinkingCount) + " thinking blocks");
		}
		if (textCount > 0)
		{
			parts.push_back(textCount == 1 ? "text" : std::to_string(textCount) + " texts");
		}
		if (toolCallCount > 0)
		{
			parts.push_back(Core::Counted(toolCallCount, "tool call"));
		}
		if (otherCount > 0)
		{
			parts.push_back(std::to_string(otherCount) + " other");
		}

		return Core::JoinedWith(parts, kMiddleDotSeparator);
	}
}
