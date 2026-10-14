#include "Sessions/Session.h"

#include "Core/Base/Strings.h"
#include "Sessions/Transcript/InjectedText.h"
#include "Sessions/Requests/RequestUsageAccumulator.h"

namespace Graphite {

	namespace {
		constexpr std::size_t kMetaTextLabelBytes = 120;

		using IndexByKey = std::unordered_map<std::string, std::size_t>;
	}

	Turn& Session::CurrentTurn()
	{
		if (m_Turns.empty())
		{
			m_Turns.emplace_back();
		}

		return m_Turns.back();
	}

	void Session::AddUser(const TranscriptRecord& record)
	{
		bool startsTurn = false;
		std::string prompt;

		for (const ContentBlock& block : record.Blocks)
		{
			if (block.Kind == BlockKind::ToolResult)
			{
				const IndexByKey::const_iterator foundTool = m_ToolById.find(block.ToolUseId);

				if (foundTool == m_ToolById.end())
				{
					continue;
				}

				const auto& [toolId, toolIndex] = *foundTool;
				ToolCall& tool = m_Tools[toolIndex];
				tool.HasResult = true;
				tool.IsError = block.IsError;
				tool.Result = block.Text;
				tool.ResultJson = block.ResultJson;
				tool.ResultBytes = block.Text.size();
				tool.FinishedAt = record.Timestamp;
			}
			else if (block.Kind == BlockKind::Text)
			{
				if (record.IsCompactSummary)
				{
					AttachCompactSummary(block.Text, record.Timestamp);
					continue;
				}

				if (record.IsMeta || IsInjectedText(block.Text))
				{
					const std::string label = IsInjectedText(block.Text)
												  ? InjectedLabel(block.Text)
												  : FirstLineLabel(block.Text, kMetaTextLabelBytes);
					CurrentTurn().Items.push_back(
						{.Kind = ItemKind::Injected, .Text = block.Text, .Label = label, .At = record.Timestamp});
					continue;
				}

				startsTurn = true;

				Core::AppendSeparated(prompt, "\n", block.Text);
			}
		}

		if (startsTurn)
		{
			Turn turn;
			turn.Prompt = std::move(prompt);
			turn.StartedAt = record.Timestamp;
			m_Turns.push_back(std::move(turn));
		}
	}

	void Session::AddQueued(const TranscriptRecord& record)
	{
		CurrentTurn().Items.push_back({.Kind = ItemKind::Queued, .Text = record.QueuedPrompt, .At = record.Timestamp});
	}

	void Session::AddCompaction(const TranscriptRecord& record)
	{
		CurrentTurn().Items.push_back(
			{.Kind = ItemKind::Compaction, .At = record.Timestamp, .Compaction = record.Compaction});
	}

	void Session::AttachCompactSummary(const std::string& text, const std::string& at)
	{
		Turn& turn = CurrentTurn();

		for (std::vector<TurnItem>::reverse_iterator item = turn.Items.rbegin(); item != turn.Items.rend(); ++item)
		{
			if (item->Kind != ItemKind::Compaction)
			{
				continue;
			}

			if (item->Text.empty())
			{
				item->Text = text;
				return;
			}

			break;
		}

		turn.Items.push_back({.Kind = ItemKind::Compaction, .Text = text, .At = at});
	}

	void Session::AddAssistant(const TranscriptRecord& record)
	{
		Turn& turn = CurrentTurn();
		const std::string& key = record.RequestId.empty() ? record.Uuid : record.RequestId;

		if (record.Usage.has_value())
		{
			const IndexByKey::const_iterator seenRequest =
				key.empty() ? m_RequestTurn.end() : m_RequestTurn.find(key);

			if (seenRequest == m_RequestTurn.end())
			{
				RequestUsage& request = turn.Requests[key];
				request = {.Model = record.Model, .Fast = record.Fast};
				RequestUsageAccumulator counted;
				counted.Add(*record.Usage, &turn.Usage[{record.Model, record.Fast}]);
				request.Usage = counted.GetCounts();

				if (!key.empty())
				{
					m_RequestTurn.emplace(key, m_Turns.size() - 1);
				}
			}
			else
			{
				const auto& [seenKey, ownerTurnIndex] = *seenRequest;
				Turn& owner = m_Turns[ownerTurnIndex];
				RequestUsage& request = owner.Requests[key];
				RequestUsageAccumulator counted{request.Usage};
				counted.Add(*record.Usage, &owner.Usage[{request.Model, request.Fast}]);
				request.Usage = counted.GetCounts();
			}
		}

		for (const ContentBlock& block : record.Blocks)
		{
			switch (block.Kind)
			{
			case BlockKind::Text:
				turn.Items.push_back(
					{.Kind = ItemKind::Text, .Text = block.Text, .At = record.Timestamp, .RequestId = key});
				break;
			case BlockKind::Thinking:
				turn.Items.push_back(
					{.Kind = ItemKind::Thinking, .Text = block.Text, .At = record.Timestamp, .RequestId = key});
				break;
			case BlockKind::ToolUse:
			{
				ToolCall tool;
				tool.Id = block.ToolUseId;
				tool.Name = block.ToolName;
				tool.InputJson = block.InputJson;
				tool.StartedAt = record.Timestamp;

				m_ToolById[tool.Id] = m_Tools.size();
				turn.Items.push_back(
					{.Kind = ItemKind::Tool, .Tool = m_Tools.size(), .At = record.Timestamp, .RequestId = key});
				m_Tools.push_back(std::move(tool));
				break;
			}
			case BlockKind::ToolResult:
			case BlockKind::Image:
				break;
			case BlockKind::Other:
				turn.Items.push_back(
					{.Kind = ItemKind::Other, .Text = block.Text, .At = record.Timestamp, .RequestId = key});
				break;
			}
		}
	}

	void Session::Append(const std::vector<TranscriptRecord>& records)
	{
		if (records.empty())
		{
			return;
		}

		for (const TranscriptRecord& record : records)
		{
			if (record.Timestamp > m_LastWrittenAt)
			{
				m_LastWrittenAt = record.Timestamp;
			}

			if (record.Kind == RecordKind::User)
			{
				AddUser(record);
			}
			else if (record.Kind == RecordKind::Assistant)
			{
				AddAssistant(record);
			}
			else if (record.Kind == RecordKind::Attachment && !record.QueuedPrompt.empty())
			{
				AddQueued(record);
			}
			else if (record.Kind == RecordKind::System && record.IsCompactBoundary)
			{
				AddCompaction(record);
			}
		}

		++m_Version;
	}

	void Session::Reset()
	{
		m_Turns.clear();
		m_Tools.clear();
		m_ToolById.clear();
		m_RequestTurn.clear();
		m_LastWrittenAt.clear();
		m_Version = 0;
		++m_Generation;
	}
}
