#include "Sessions/Session.h"

namespace Graphite {

	Turn& Session::CurrentTurn()
	{
		if (m_Turns.empty())
			m_Turns.emplace_back();

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
				const auto found = m_ToolById.find(block.ToolUseId);

				if (found == m_ToolById.end())
					continue;

				ToolCall& tool = m_Tools[found->second];
				tool.HasResult = true;
				tool.IsError = block.IsError;
				tool.Result = block.Text;
				tool.FinishedAt = record.Timestamp;
			}
			else if (block.Kind == BlockKind::Text && !record.IsMeta)
			{
				startsTurn = true;

				if (!prompt.empty())
					prompt += '\n';
				prompt += block.Text;
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

	void Session::AddAssistant(const TranscriptRecord& record)
	{
		Turn& turn = CurrentTurn();

		// Same rule as UsageTally: a response's usage rides on every record of it.
		if (record.Usage.has_value())
		{
			const std::string& key = record.RequestId.empty() ? record.Uuid : record.RequestId;

			if (key.empty() || m_SeenRequests.insert(key).second)
				turn.Usage[{record.Model, record.Fast}] += *record.Usage;
		}

		for (const ContentBlock& block : record.Blocks)
		{
			switch (block.Kind)
			{
			case BlockKind::Text:
				turn.Items.push_back({.Kind = ItemKind::Text, .Text = block.Text});
				break;
			case BlockKind::Thinking:
				turn.Items.push_back({.Kind = ItemKind::Thinking, .Text = block.Text});
				break;
			case BlockKind::ToolUse:
			{
				ToolCall tool;
				tool.Id = block.ToolUseId;
				tool.Name = block.ToolName;
				tool.InputJson = block.InputJson;
				tool.StartedAt = record.Timestamp;

				m_ToolById[tool.Id] = m_Tools.size();
				turn.Items.push_back({.Kind = ItemKind::Tool, .Tool = m_Tools.size()});
				m_Tools.push_back(std::move(tool));
				break;
			}
			case BlockKind::ToolResult:
				break;
			case BlockKind::Other:
				turn.Items.push_back({.Kind = ItemKind::Other, .Text = block.Text});
				break;
			}
		}
	}

	void Session::Append(const std::vector<TranscriptRecord>& records)
	{
		if (records.empty())
			return;

		for (const TranscriptRecord& record : records)
		{
			if (record.Kind == RecordKind::User)
				AddUser(record);
			else if (record.Kind == RecordKind::Assistant)
				AddAssistant(record);

			// Attachments, system records and unknown record types are bookkeeping — modes,
			// reminders, snapshots — not conversation. The summary reads what it needs of
			// them; the conversation view does not draw them.
		}

		++m_Version;
	}
}
