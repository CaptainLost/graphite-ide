#include "Sessions/SessionActivity.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/Transcript/InjectedText.h"
#include "Sessions/Transcript/JsonAccess.h"

#include <algorithm>

namespace Graphite {

	namespace {
		constexpr std::string_view kMcpPrefix = "mcp__";
		constexpr std::string_view kMcpSeparator = "__";
		constexpr std::string_view kMcpGroupPrefix = "mcp:";
		constexpr std::string_view kSkillToolName = "Skill";
		constexpr const char* kSkillInputField = "skill";

		void MergeHours(std::map<UtcHour, HourActivity>& into, const std::map<UtcHour, HourActivity>& from)
		{
			for (const auto& [hour, activity] : from)
			{
				HourActivity& target = into[hour];

				for (const auto& [usageKey, counts] : activity.Usage)
				{
					target.Usage[usageKey] += counts;
				}

				for (const auto& [usageKey, requests] : activity.RequestsByModel)
				{
					target.RequestsByModel[usageKey] += requests;
				}

				target.Requests += activity.Requests;
				target.Prompts += activity.Prompts;
			}
		}

		std::chrono::minutes ActiveTimeOf(std::vector<std::chrono::sys_time<std::chrono::milliseconds>> times)
		{
			std::sort(times.begin(), times.end());
			std::chrono::milliseconds active{0};

			for (std::size_t index = 1; index < times.size(); ++index)
			{
				const std::chrono::milliseconds gap = times[index] - times[index - 1];

				if (gap < kActiveGap)
				{
					active += gap;
				}
			}

			return std::chrono::floor<std::chrono::minutes>(active);
		}
	}

	std::optional<std::string> TypedPrompt(const TranscriptRecord& record)
	{
		if (record.Kind != RecordKind::User || record.IsMeta)
		{
			return std::nullopt;
		}

		for (const ContentBlock& block : record.Blocks)
		{
			if (block.Kind == BlockKind::Text && !block.Text.empty() && !IsInjectedText(block.Text))
			{
				return block.Text;
			}
		}

		return std::nullopt;
	}

	std::string ToolGroupName(std::string_view toolName)
	{
		if (!toolName.starts_with(kMcpPrefix))
		{
			return std::string{toolName};
		}

		const std::string_view qualified = toolName.substr(kMcpPrefix.size());
		const std::string_view server = qualified.substr(0, qualified.find(kMcpSeparator));
		return std::string{kMcpGroupPrefix} + std::string{server};
	}

	std::chrono::sys_days UtcToday()
	{
		return std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
	}

	std::chrono::sys_days NewestUsageDay(std::chrono::sys_days today)
	{
		return today + std::chrono::days{1};
	}

	bool IsWithinUsageRange(UtcHour hour, std::chrono::sys_days today)
	{
		const UtcHour oldest{kOldestUsageDay};
		const UtcHour pastNewest{NewestUsageDay(today) + std::chrono::days{1}};
		return hour >= oldest && hour < pastNewest;
	}

	ActivityRecorder::ActivityRecorder(TranscriptThread thread) : m_Thread(thread) {}

	void ActivityRecorder::Add(const TranscriptRecord& record)
	{
		const std::optional<std::chrono::sys_time<std::chrono::milliseconds>> time = ParseTimestamp(record.Timestamp);
		std::optional<UtcHour> hour;

		if (time.has_value() && IsWithinUsageRange(std::chrono::floor<std::chrono::hours>(*time), UtcToday()))
		{
			hour = std::chrono::floor<std::chrono::hours>(*time);
		}

		AddUsage(record, hour);

		for (const ContentBlock& block : record.Blocks)
		{
			if (block.Kind == BlockKind::ToolUse)
			{
				AddToolUse(block, record.Timestamp);
			}
			else if (block.Kind == BlockKind::ToolResult && block.IsError)
			{
				AddToolResult(block);
			}
		}

		if (m_Thread != TranscriptThread::Main)
		{
			return;
		}

		if (hour.has_value() && TypedPrompt(record).has_value())
		{
			++m_Hours[*hour].Prompts;
		}

		if (record.IsCompactBoundary)
		{
			++m_Compactions;
		}

		if (time.has_value())
		{
			m_Times.push_back(*time);
		}
	}

	void ActivityRecorder::AddTo(SessionActivity& activity) const
	{
		MergeHours(m_Thread == TranscriptThread::Main ? activity.MainHours : activity.SubagentHours, m_Hours);

		for (const auto& [cwd, usage] : m_UsageByCwd)
		{
			UsageByModel& target = activity.MainUsageByCwd[cwd];

			for (const auto& [usageKey, counts] : usage)
			{
				target[usageKey] += counts;
			}
		}

		for (const auto& [name, tally] : m_Tools)
		{
			ToolTally& target = activity.Tools[name];
			target.Calls += tally.Calls;
			target.Failed += tally.Failed;
		}

		for (const auto& [name, tally] : m_Skills)
		{
			SkillTally& target = activity.Skills[name];
			target.Calls += tally.Calls;
			target.LastCalledAt = std::max(target.LastCalledAt, tally.LastCalledAt);
		}

		activity.Compactions += m_Compactions;
		activity.ActiveTime += ActiveTimeOf(m_Times);
	}

	void ActivityRecorder::AddUsage(const TranscriptRecord& record, const std::optional<UtcHour>& hour)
	{
		if (!record.Usage.has_value())
		{
			return;
		}

		const std::string& requestKey = record.RequestId.empty() ? record.Uuid : record.RequestId;
		const UsageKey usageKey{record.Model, record.Fast};

		if (requestKey.empty())
		{
			constexpr bool kIsNewRequest = true;
			Charge(hour, record.Cwd, usageKey, *record.Usage, kIsNewRequest);
			return;
		}

		const auto [entry, isNewRequest] = m_Requests.try_emplace(requestKey, Request{usageKey, {}, hour, record.Cwd});
		Request& request = entry->second;

		const TokenCounts before = request.Usage.GetCounts();
		request.Usage.Add(*record.Usage);
		TokenCounts growth = request.Usage.GetCounts();
		growth -= before;

		Charge(request.Hour, request.Cwd, request.Key, growth, isNewRequest);
	}

	void ActivityRecorder::Charge(const std::optional<UtcHour>& hour, const std::string& cwd, const UsageKey& usageKey,
								  const TokenCounts& counts, bool isNewRequest)
	{
		if (hour.has_value())
		{
			HourActivity& hourActivity = m_Hours[*hour];
			hourActivity.Usage[usageKey] += counts;

			if (isNewRequest)
			{
				++hourActivity.Requests;
				++hourActivity.RequestsByModel[usageKey];
			}
		}

		if (m_Thread == TranscriptThread::Main)
		{
			m_UsageByCwd[cwd][usageKey] += counts;
		}
	}

	void ActivityRecorder::AddToolUse(const ContentBlock& block, const std::string& timestamp)
	{
		const std::string group = ToolGroupName(block.ToolName);

		if (!block.ToolUseId.empty() && !m_ToolGroupById.try_emplace(block.ToolUseId, group).second)
		{
			return;
		}

		++m_Tools[group].Calls;

		if (block.ToolName != kSkillToolName)
		{
			return;
		}

		const std::string skill = StringAt(ObjectOf(block.InputJson), kSkillInputField);

		if (skill.empty())
		{
			return;
		}

		SkillTally& tally = m_Skills[skill];
		++tally.Calls;
		tally.LastCalledAt = std::max(tally.LastCalledAt, timestamp);
	}

	void ActivityRecorder::AddToolResult(const ContentBlock& block)
	{
		const auto toolGroup = m_ToolGroupById.find(block.ToolUseId);

		if (toolGroup == m_ToolGroupById.end() || !m_FailedToolUseIds.insert(block.ToolUseId).second)
		{
			return;
		}

		++m_Tools[toolGroup->second].Failed;
	}
}
