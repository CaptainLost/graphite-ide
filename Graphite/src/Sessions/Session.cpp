#include "Sessions/Session.h"

#include "Sessions/InjectedText.h"

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
				tool.ResultJson = block.ResultJson;
				tool.ResultBytes = block.Text.size();
				tool.FinishedAt = record.Timestamp;
			}
			else if (block.Kind == BlockKind::Text)
			{
				// Words Claude Code put in the user's mouth fold into the turn they arrived in;
				// only what the user typed starts a turn.
				if (record.IsMeta || IsInjectedText(block.Text))
				{
					// Meta text with no known opening, a skill's body say, has no kind to name, so
					// its first line says what it is.
					const std::string label = IsInjectedText(block.Text)
												  ? InjectedLabel(block.Text)
												  : FirstLineLabel(block.Text, 120);
					CurrentTurn().Items.push_back(
						{.Kind = ItemKind::Injected, .Text = block.Text, .Label = label, .At = record.Timestamp});
					continue;
				}

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

	void Session::AddQueued(const TranscriptRecord& record)
	{
		CurrentTurn().Items.push_back({.Kind = ItemKind::Queued, .Text = record.QueuedPrompt, .At = record.Timestamp});
	}

	void Session::AddAssistant(const TranscriptRecord& record)
	{
		Turn& turn = CurrentTurn();
		const std::string& key = record.RequestId.empty() ? record.Uuid : record.RequestId;

		// Same rule as UsageTally: a response's usage rides on every record of it, counted once
		// under the model its first record names, keeping the counts MergeResponseUsage picks.
		if (record.Usage.has_value())
		{
			const auto seen = key.empty() ? m_RequestTurn.end() : m_RequestTurn.find(key);

			if (seen == m_RequestTurn.end())
			{
				turn.Usage[{record.Model, record.Fast}] += *record.Usage;
				turn.Requests[key] = {record.Model, record.Fast, *record.Usage};

				if (!key.empty())
					m_RequestTurn.emplace(key, m_Turns.size() - 1);
			}
			else
			{
				Turn& owner = m_Turns[seen->second];
				RequestUsage& request = owner.Requests[key];
				const TokenCounts merged = MergeResponseUsage(request.Usage, *record.Usage);
				TokenCounts& total = owner.Usage[{request.Model, request.Fast}];
				total -= request.Usage;
				total += merged;
				request.Usage = merged;
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
				// Neither is something a response writes; the conversation has no row for them.
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
			return;

		for (const TranscriptRecord& record : records)
		{
			if (record.Kind == RecordKind::User)
				AddUser(record);
			else if (record.Kind == RecordKind::Assistant)
				AddAssistant(record);
			else if (record.Kind == RecordKind::Attachment && !record.QueuedPrompt.empty())
				AddQueued(record);

			// Other attachments, system records and unknown record types are bookkeeping —
			// modes, reminders, snapshots — not conversation. The summary reads what it needs
			// of them; the conversation view does not draw them.
		}

		++m_Version;
	}

	void Session::Reset()
	{
		m_Turns.clear();
		m_Tools.clear();
		m_ToolById.clear();
		m_RequestTurn.clear();
		m_Version = 0;
		++m_Generation;
	}
}
