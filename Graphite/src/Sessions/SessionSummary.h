#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/Requests/RequestUsageAccumulator.h"
#include "Sessions/Transcript/TranscriptRecord.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Graphite {

	// WARNING: Claude Code writes one record per content block of a response and puts the response's usage
	// on each, so a sum over records counts one request two to five times. MergeResponseUsage says which
	// counts a request keeps.
	class UsageTally
	{
	public:
		void Add(const TranscriptRecord& record);

		const UsageByModel& Get() const { return m_Usage; }

	private:
		// WARNING: The first record with usage names the model, since a refused response's last record names
		// <synthetic>.
		struct Counted
		{
			UsageKey Key;
			RequestUsageAccumulator Usage;
		};

		std::unordered_map<std::string, Counted> m_Counted;
		UsageByModel m_Usage;
	};

	struct Fingerprint
	{
		std::int64_t WriteTicks = 0;
		std::uint64_t Bytes = 0;
		std::size_t Files = 0;

		friend bool operator==(const Fingerprint&, const Fingerprint&) = default;
	};

	std::filesystem::path SubagentDirectory(const std::filesystem::path& sessionFile);

	Fingerprint FingerprintOf(const std::filesystem::path& sessionFile);

	struct SessionSummary
	{
		std::string Id;
		std::filesystem::path File;
		std::filesystem::path Cwd;
		std::string Title;
		std::string FirstPrompt;
		std::string StartedAt;
		std::string EndedAt;
		UsageByModel Main;
		UsageByModel Subagents;
		std::size_t SubagentCount = 0;
		std::size_t SkippedLines = 0;
		Fingerprint Print;

		std::string DisplayTitle() const;

		bool IsEmpty() const;

		friend bool operator==(const SessionSummary&, const SessionSummary&) = default;
	};

	SessionSummary SummariseSession(const std::filesystem::path& sessionFile);

	// WARNING: A summary of an unreadable file would be an empty session with a valid fingerprint, and a
	// caller that remembers it would never look at the file again once it becomes readable.
	std::optional<SessionSummary> TrySummariseSession(const std::filesystem::path& sessionFile);

	// WARNING: Claude Code writes session ids in lower case, and a UUID formatted elsewhere may not be.
	bool SameSessionId(std::string_view firstId, std::string_view secondId);

	std::optional<std::chrono::sys_time<std::chrono::milliseconds>> ParseTimestamp(std::string_view text);

	inline constexpr std::chrono::minutes kLiveWindow{2};

	bool IsWithinDirectory(const std::filesystem::path& inner, const std::filesystem::path& outer);

	std::filesystem::path::string_type DirectoryKey(const std::filesystem::path& directory);

	// WARNING: ImGui draws a character split in half as a replacement glyph.
	std::string TruncateUtf8(std::string_view text, std::size_t maxBytes);
}
