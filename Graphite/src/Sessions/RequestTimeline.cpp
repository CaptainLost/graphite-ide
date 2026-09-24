#include "Sessions/RequestTimeline.h"

#include "Sessions/InjectedText.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SessionText.h"
#include "Sessions/ToolFacts.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

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

		std::string ToolCount(std::size_t count)
		{
			return std::to_string(count) + (count == 1 ? " tool" : " tools");
		}

		// These are names only: a deferred tool's schema reaches the model when it is loaded,
		// not when it is announced.
		std::string DeferredNames(std::size_t count)
		{
			return std::to_string(count) + (count == 1 ? " deferred tool name" : " deferred tool names");
		}

		std::string ToolsLoadedLabel(const std::vector<std::string>& added, const std::vector<std::string>& removed)
		{
			// U+2212 MINUS SIGN, not a hyphen: this is a count going down, drawn the same way a
			// negative context delta is drawn in the request views.
			const std::string minus = "\xe2\x88\x92";

			if (added.empty() && removed.empty())
				return "tools unchanged";

			if (added.empty())
				return minus + ToolCount(removed.size()) + " removed";

			std::string label = DeferredNames(added.size()) + ": " + JoinNames(added, 5);

			if (!removed.empty())
				label += ", " + minus + std::to_string(removed.size()) + " removed";

			return label;
		}

		// Characters, not bytes: a reply in Polish has more bytes than letters.
		std::size_t CodePoints(std::string_view text)
		{
			return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c)
														  { return (static_cast<unsigned char>(c) & 0xC0u) != 0x80u; }));
		}

		// "Image (png, 212.0 KB)", leaving out whatever the block did not say.
		std::string ImageLabel(std::string_view mediaType, std::size_t bytes)
		{
			std::vector<std::string> parts;

			if (!mediaType.empty())
				parts.emplace_back(mediaType.starts_with("image/") ? mediaType.substr(6) : mediaType);
			if (bytes > 0)
				parts.push_back(FormatBytes(bytes));

			std::string label = "Image";

			for (std::size_t i = 0; i < parts.size(); ++i)
				label += (i == 0 ? " (" : ", ") + parts[i];

			return parts.empty() ? label : label + ")";
		}

		// An image in the sent text: what it is and how big, since its pixels are not text.
		std::string ImageText(std::string_view mediaType, std::size_t bytes)
		{
			std::string text = "[" + (mediaType.empty() ? std::string{"image"} : std::string{mediaType});

			if (bytes > 0)
				text += ", " + FormatBytes(bytes);

			return text + "]";
		}

		// A tool call's input laid out two spaces an indent; the input as written when it does
		// not parse.
		std::string PrettyJson(const std::string& json)
		{
			const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);

			return parsed.is_discarded() ? json : parsed.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
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

	// Recomputes everything Usage derives, for a request that just adopted one — whether at
	// creation or from a later record of the same key.
	void RequestTimeline::ApplyUsage(ApiRequest& request, std::size_t index, const TokenCounts& usage)
	{
		request.Usage = usage;
		request.Context =
			request.Usage.Input + request.Usage.CacheWrite5m + request.Usage.CacheWrite1h + request.Usage.CacheRead;
		request.ContextDelta = ContextDeltaFor(index);
		request.CachedShare = request.Context > 0
								  ? static_cast<double>(request.Usage.CacheRead) / static_cast<double>(request.Context)
								  : 0.0;
		request.CacheRebuilt =
			request.Context > 0 && (request.Usage.CacheWrite5m + request.Usage.CacheWrite1h) * 2 >= request.Context;
		EstimateTokens(request);
	}

	std::optional<std::int64_t> RequestTimeline::ContextDeltaFor(std::size_t index) const
	{
		// A request with no context — no usage of its own, or the all-zero usage of a response
		// Claude Code wrote itself for an interrupt or an API error — is neither measured nor
		// a baseline: its zero would read as a drop, and the next request as adding its whole
		// context, when no one actually caused either.
		if (m_Requests[index].Context == 0)
			return std::nullopt;

		for (std::size_t j = index; j-- > 0;)
		{
			if (m_HasUsage[j] && m_Requests[j].Context > 0)
				return static_cast<std::int64_t>(m_Requests[index].Context) -
					   static_cast<std::int64_t>(m_Requests[j].Context);
		}

		return std::nullopt;
	}

	void RequestTimeline::AddAssistant(const TranscriptRecord& record, bool& changed)
	{
		const std::string& key = record.RequestId.empty() ? record.Uuid : record.RequestId;

		// An empty key means Claude Code gave this response neither a requestId nor a uuid.
		// Session treats that as "never deduplicate" for the same reason: two such responses
		// are not the same request just because they share the one key every keyless record
		// happens to get.
		const bool keyed = !key.empty();
		const auto found = keyed ? m_ByKey.find(key) : m_ByKey.end();
		ApiRequest* request = nullptr;
		std::size_t index = 0;

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

			fresh.Sent = std::move(m_Pending);
			m_Pending.clear();
			fresh.ToolsAvailable = m_Tools.size();
			fresh.ToolsLoaded = std::exchange(m_PendingLoaded, 0);

			const bool hasUsage = record.Usage.has_value();

			m_Requests.push_back(std::move(fresh));
			m_HasUsage.push_back(hasUsage);
			index = m_Requests.size() - 1;
			request = &m_Requests[index];

			// A request with no usage yet has nothing to derive a Context or a delta from:
			// ApplyUsage waits for one, whether this record has it or a later one adopts it.
			if (hasUsage)
				ApplyUsage(*request, index, *record.Usage);

			if (keyed)
				m_ByKey[key] = index;

			changed = true;
		}
		else
		{
			index = found->second;
			request = &m_Requests[index];

			// Session's rule: usage rides on every record of a response, so the first record
			// that carries one is authoritative — a later duplicate must not overwrite it.
			if (!m_HasUsage[index] && record.Usage.has_value())
			{
				ApplyUsage(*request, index, *record.Usage);
				m_HasUsage[index] = true;
				changed = true;
			}
		}

		// A response written as several records repeats its stop reason on the last one; a
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
					{.Kind = ResponseKind::Thinking,
					 .Label = block.Text.empty() ? "thinking"
												 : "thinking (" + std::to_string(CodePoints(block.Text)) + " chars)",
					 .Text = block.Text});
				changed = true;
				break;
			case BlockKind::Text:
				request->Response.push_back(
					{.Kind = ResponseKind::Text, .Label = std::to_string(CodePoints(block.Text)) + " chars", .Text = block.Text});
				changed = true;
				break;
			case BlockKind::ToolUse:
			{
				// The summary is all a result needs of its call later; the input is kept once, laid out, as the
				// text the model wrote.
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
				// An assistant record carries neither a tool result nor an image; nothing to add.
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
					label = toolName + "  " + found->second.Summary;
				}

				// The result's text only: the structured result beside it is Claude Code's own
				// record — an Edit's carries the whole file — and never reaches the model.
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
				// Base64 carries three bytes in every four characters.
				const std::size_t bytes = block.DataBytes * 3 / 4;
				m_Pending.push_back({.Kind = SentKind::Image,
									 .Label = ImageLabel(block.Text, bytes),
									 .Bytes = bytes,
									 .Text = ImageText(block.Text, bytes)});
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
					m_Pending.push_back(
						{.Kind = SentKind::Injected, .Label = label, .Bytes = block.Text.size(), .Text = block.Text});
				}
				else
				{
					m_Pending.push_back({.Kind = SentKind::Prompt,
										 .Label = FirstLineTruncated(block.Text, 120),
										 .Bytes = block.Text.size(),
										 .Text = block.Text});
				}

				changed = true;
			}
		}
	}

	void RequestTimeline::AddAttachment(const TranscriptRecord& record, bool& changed)
	{
		// Claude Code's snapshot of the prompt is read for the first request, not listed as
		// something sent in its own right.
		if (record.AttachmentType == "prompt_snapshot")
			return;

		// Claude Code's own bookkeeping — a hook's outcome, a record of the deferred tools —
		// that the model never saw.
		if (!record.AttachmentSent)
			return;

		SentItem item;
		item.Text = record.AttachmentText;
		item.Bytes = item.Text.size();
		item.Approximate = record.AttachmentApproximate;

		if (!record.QueuedPrompt.empty())
		{
			item.Kind = SentKind::QueuedPrompt;
			item.Label = FirstLineTruncated(record.QueuedPrompt, 120);
		}
		else if (record.AttachmentType == "deferred_tools_delta")
		{
			item.Kind = SentKind::ToolsLoaded;
			item.Label = ToolsLoadedLabel(record.AttachmentNames, record.AttachmentRemoved);

			m_PendingLoaded += record.AttachmentNames.size();

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

			// A malformed timestamp is not "no timestamp": keeping the last one that actually
			// parsed means the next request's Waited is still measured against real time,
			// rather than becoming uncomputable because of one bad record in between.
			if (ParseTimestamp(record.Timestamp).has_value())
				m_LastTimestamp = record.Timestamp;
		}

		if (changed)
			++m_Version;
	}

	void RequestTimeline::Reset()
	{
		m_Requests.clear();
		m_ByKey.clear();
		m_HasUsage.clear();
		m_Pending.clear();
		m_ToolCalls.clear();
		m_Tools.clear();
		m_PendingLoaded = 0;
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

	std::string ToolsAvailableLabel(const ApiRequest& request)
	{
		std::string label = "Tools available: " + std::to_string(request.ToolsAvailable);

		if (request.ToolsLoaded > 0)
			label += " (+" + std::to_string(request.ToolsLoaded) + " loaded)";

		return label;
	}
}
