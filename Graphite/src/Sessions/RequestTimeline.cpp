#include "Sessions/RequestTimeline.h"

#include "Sessions/InjectedText.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"

#include <algorithm>
#include <unordered_set>

namespace Graphite {

	namespace {

		std::string FirstLineTruncated(std::string_view text, std::size_t maxBytes)
		{
			const std::size_t newline = text.find('\n');
			const std::string_view line = newline == std::string_view::npos ? text : text.substr(0, newline);
			return TruncateUtf8(line, maxBytes);
		}

		// Up to `maxNames` names, comma-separated, then an ellipsis for the rest.
		std::string JoinNames(const std::vector<std::string>& names, std::size_t maxNames)
		{
			std::string joined;
			const std::size_t shown = std::min(names.size(), maxNames);

			for (std::size_t i = 0; i < shown; ++i)
			{
				if (i > 0)
					joined += ", ";
				joined += names[i];
			}

			if (names.size() > maxNames)
				joined += ", \xe2\x80\xa6";

			return joined;
		}

		std::string ToolsLoadedLabel(const std::vector<std::string>& added, const std::vector<std::string>& removed)
		{
			std::string label = "+" + std::to_string(added.size()) + " tools: " + JoinNames(added, 5);

			// U+2212 MINUS SIGN, not a hyphen: this is a count going down, drawn the same way a
			// negative delta is drawn elsewhere in the request views.
			if (!removed.empty())
				label += ", \xe2\x88\x92" + std::to_string(removed.size()) + " removed";

			return label;
		}

		std::string McpLabel(const std::vector<std::string>& names)
		{
			std::string joined;

			for (std::size_t i = 0; i < names.size(); ++i)
			{
				if (i > 0)
					joined += ", ";
				joined += names[i];
			}

			return "MCP: " + joined;
		}
	}

	void RequestTimeline::EstimateTokens(ApiRequest& request) const
	{
		for (SentItem& item : request.Sent)
			item.EstimatedTokens = 0;

		// A context that shrank, or nothing sent with a byte to measure, gives no estimate:
		// splitting a negative or unmeasurable delta across items would be a made-up number.
		if (!request.ContextDelta.has_value() || *request.ContextDelta <= 0)
			return;

		std::uint64_t totalBytes = 0;

		for (const SentItem& item : request.Sent)
			totalBytes += item.Bytes;

		if (totalBytes == 0)
			return;

		const std::int64_t delta = *request.ContextDelta;
		std::int64_t assigned = 0;
		std::size_t lastWithBytes = request.Sent.size();

		for (std::size_t i = 0; i < request.Sent.size(); ++i)
		{
			SentItem& item = request.Sent[i];

			if (item.Bytes == 0)
				continue;

			const std::int64_t estimate =
				delta * static_cast<std::int64_t>(item.Bytes) / static_cast<std::int64_t>(totalBytes);
			item.EstimatedTokens = estimate;
			assigned += estimate;
			lastWithBytes = i;
		}

		// Rounding down on every item leaves a remainder; the last item that had a byte to
		// measure absorbs it, so the estimates sum to exactly the delta.
		if (lastWithBytes < request.Sent.size())
			request.Sent[lastWithBytes].EstimatedTokens += delta - assigned;
	}

	void RequestTimeline::AddAssistant(const TranscriptRecord& record, bool& changed)
	{
		const std::string& key = record.RequestId.empty() ? record.Uuid : record.RequestId;
		const auto found = m_ByKey.find(key);
		ApiRequest* request = nullptr;

		if (found == m_ByKey.end())
		{
			ApiRequest fresh;
			fresh.Index = m_Requests.size() + 1;
			fresh.Key = key;
			fresh.Model = record.Model;
			fresh.Fast = record.Fast;
			fresh.RespondedAt = record.Timestamp;

			const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> respondedAt =
				ParseTimestamp(record.Timestamp);
			const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> previousAt =
				ParseTimestamp(m_LastTimestamp);

			if (respondedAt.has_value() && previousAt.has_value())
			{
				const std::chrono::milliseconds waited = *respondedAt - *previousAt;

				if (waited.count() >= 0)
					fresh.Waited = waited;
			}

			if (record.Usage.has_value())
				fresh.Usage = *record.Usage;

			fresh.Context =
				fresh.Usage.Input + fresh.Usage.CacheWrite5m + fresh.Usage.CacheWrite1h + fresh.Usage.CacheRead;

			if (!m_Requests.empty())
				fresh.ContextDelta = static_cast<std::int64_t>(fresh.Context) -
									 static_cast<std::int64_t>(m_Requests.back().Context);

			fresh.CachedShare = fresh.Context > 0
									? static_cast<double>(fresh.Usage.CacheRead) / static_cast<double>(fresh.Context)
									: 0.0;
			fresh.CacheRebuilt =
				fresh.Context > 0 && (fresh.Usage.CacheWrite5m + fresh.Usage.CacheWrite1h) * 2 >= fresh.Context;
			fresh.Sent = std::move(m_Pending);
			m_Pending.clear();
			fresh.ToolsAvailable = m_Tools.size();

			EstimateTokens(fresh);

			m_Requests.push_back(std::move(fresh));
			m_ByKey[key] = m_Requests.size() - 1;
			request = &m_Requests.back();
			changed = true;
		}
		else
			request = &m_Requests[found->second];

		// A response written as several records repeats its stop reason on the last one; a
		// blank one on an earlier record must not erase it.
		if (!record.StopReason.empty())
			request->StopReason = record.StopReason;

		for (const ContentBlock& block : record.Blocks)
		{
			switch (block.Kind)
			{
			case BlockKind::Thinking:
				request->Response.push_back(
					{.Kind = ResponseKind::Thinking,
					 .Label = block.Text.empty() ? "thinking"
												 : "thinking (" + std::to_string(block.Text.size()) + " chars)"});
				changed = true;
				break;
			case BlockKind::Text:
				request->Response.push_back(
					{.Kind = ResponseKind::Text, .Label = std::to_string(block.Text.size()) + " chars"});
				changed = true;
				break;
			case BlockKind::ToolUse:
			{
				m_ToolCalls[block.ToolUseId] = {block.ToolName, block.InputJson};
				const std::string summary = ToolSummary({.Name = block.ToolName, .InputJson = block.InputJson});
				request->Response.push_back({.Kind = ResponseKind::ToolCall,
											 .Label = block.ToolName + "  " + summary,
											 .ToolName = block.ToolName});
				changed = true;
				break;
			}
			case BlockKind::ToolResult:
				// An assistant record never carries a tool result; nothing to add.
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
				const auto found = m_ToolCalls.find(block.ToolUseId);
				std::string label = "unknown tool";
				std::string toolName;

				if (found != m_ToolCalls.end())
				{
					toolName = found->second.Name;
					const std::string summary =
						ToolSummary({.Name = found->second.Name, .InputJson = found->second.InputJson});
					label = toolName + "  " + summary;
				}

				m_Pending.push_back({.Kind = SentKind::ToolResult,
									 .Label = label,
									 .ToolName = toolName,
									 .IsError = block.IsError,
									 .Bytes = block.Text.size() + block.ResultJson.size()});
				changed = true;
			}
			else if (block.Kind == BlockKind::Text)
			{
				if (record.IsMeta || IsInjectedText(block.Text))
				{
					// Meta text with no known opening, a skill's body say, has no kind to name,
					// so its first line says what it is.
					const std::string label = IsInjectedText(block.Text) ? InjectedLabel(block.Text)
																		 : FirstLineTruncated(block.Text, 120);
					m_Pending.push_back({.Kind = SentKind::Injected, .Label = label, .Bytes = block.Text.size()});
				}
				else
				{
					m_Pending.push_back({.Kind = SentKind::Prompt,
										 .Label = FirstLineTruncated(block.Text, 120),
										 .Bytes = block.Text.size()});
				}

				changed = true;
			}
		}
	}

	void RequestTimeline::AddAttachment(const TranscriptRecord& record, bool& changed)
	{
		SentItem item;
		item.Bytes = record.Bytes;

		if (!record.QueuedPrompt.empty())
		{
			item.Kind = SentKind::QueuedPrompt;
			item.Label = FirstLineTruncated(record.QueuedPrompt, 120);
		}
		else if (record.AttachmentType == "deferred_tools_delta")
		{
			item.Kind = SentKind::ToolsLoaded;
			item.Label = ToolsLoadedLabel(record.AttachmentNames, record.AttachmentRemoved);

			for (const std::string& name : record.AttachmentNames)
				m_Tools.insert(name);
			for (const std::string& name : record.AttachmentRemoved)
				m_Tools.erase(name);
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

	void RequestTimeline::Append(const std::vector<TranscriptRecord>& records)
	{
		bool changed = false;

		for (const TranscriptRecord& record : records)
		{
			if (record.Kind == RecordKind::Assistant)
				AddAssistant(record, changed);
			else if (record.Kind == RecordKind::User)
				AddUser(record, changed);
			else if (record.Kind == RecordKind::Attachment)
				AddAttachment(record, changed);

			// System, AiTitle and unrecognised record kinds carry nothing this timeline shows.

			if (!record.Timestamp.empty())
				m_LastTimestamp = record.Timestamp;
		}

		if (changed)
			++m_Version;
	}

	void RequestTimeline::Reset()
	{
		m_Requests.clear();
		m_ByKey.clear();
		m_Pending.clear();
		m_ToolCalls.clear();
		m_Tools.clear();
		m_LastTimestamp.clear();
		++m_Version;
	}

	std::optional<std::size_t> RequestTimeline::IndexOf(std::string_view key) const
	{
		const auto found = m_ByKey.find(std::string(key));
		return found != m_ByKey.end() ? std::optional<std::size_t>{found->second} : std::nullopt;
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
		std::unordered_set<std::string> seen;

		// Chronologically, what a request sent went out before what it answered with came back.
		for (const SentItem& item : request.Sent)
		{
			if (item.Kind == SentKind::ToolResult && !item.ToolName.empty() && seen.insert(item.ToolName).second)
				names.push_back(item.ToolName);
		}

		for (const ResponseItem& item : request.Response)
		{
			if (item.Kind == ResponseKind::ToolCall && !item.ToolName.empty() && seen.insert(item.ToolName).second)
				names.push_back(item.ToolName);
		}

		return names;
	}
}
