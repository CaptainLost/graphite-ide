#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestUsageAccumulator.h"
#include "Sessions/Transcript/TranscriptRecord.h"

#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Graphite {

	using UtcHour = std::chrono::sys_time<std::chrono::hours>;

	struct HourActivity
	{
		UsageByModel Usage;
		std::size_t Requests = 0;
		std::size_t Prompts = 0;

		friend bool operator==(const HourActivity&, const HourActivity&) = default;
	};

	struct ToolTally
	{
		std::size_t Calls = 0;
		std::size_t Failed = 0;

		friend bool operator==(const ToolTally&, const ToolTally&) = default;
	};

	struct SkillTally
	{
		std::size_t Calls = 0;
		std::string LastCalledAt;

		friend bool operator==(const SkillTally&, const SkillTally&) = default;
	};

	struct SessionActivity
	{
		std::map<UtcHour, HourActivity> MainHours;
		std::map<UtcHour, HourActivity> SubagentHours;
		// WARNING: Keyed by the cwd as the transcript writes it, in UTF-8, so a key is not a path to compare.
		std::map<std::string, UsageByModel> MainUsageByCwd;
		std::map<std::string, ToolTally> Tools;
		std::map<std::string, SkillTally> Skills;
		std::size_t Compactions = 0;
		std::chrono::minutes ActiveTime{0};

		friend bool operator==(const SessionActivity&, const SessionActivity&) = default;
	};

	enum class TranscriptThread
	{
		Main,
		Subagent
	};

	std::optional<std::string> TypedPrompt(const TranscriptRecord& record);

	std::string ToolGroupName(std::string_view toolName);

	// WARNING: A request lands whole in the hour and cwd of its first record with usage, and later records add
	// only what MergeResponseUsage grows it by, so the buckets sum to UsageTally's totals for the same file.
	class ActivityRecorder
	{
	public:
		explicit ActivityRecorder(TranscriptThread thread);

		void Add(const TranscriptRecord& record);

		void AddTo(SessionActivity& activity) const;

	private:
		struct Request
		{
			UsageKey Key;
			RequestUsageAccumulator Usage;
			std::optional<UtcHour> Hour;
			std::string Cwd;
		};

		void AddUsage(const TranscriptRecord& record, const std::optional<UtcHour>& hour);
		void Charge(const std::optional<UtcHour>& hour, const std::string& cwd, const UsageKey& usageKey,
					const TokenCounts& counts, bool isNewRequest);
		void AddToolUse(const ContentBlock& block, const std::string& timestamp);
		void AddToolResult(const ContentBlock& block);

		TranscriptThread m_Thread;
		std::unordered_map<std::string, Request> m_Requests;
		std::map<UtcHour, HourActivity> m_Hours;
		std::map<std::string, UsageByModel> m_UsageByCwd;
		std::unordered_map<std::string, std::string> m_ToolGroupById;
		std::unordered_set<std::string> m_FailedToolUseIds;
		std::map<std::string, ToolTally> m_Tools;
		std::map<std::string, SkillTally> m_Skills;
		std::size_t m_Compactions = 0;
		std::vector<std::chrono::sys_time<std::chrono::milliseconds>> m_Times;
	};

	inline constexpr std::chrono::minutes kActiveGap{10};
}
