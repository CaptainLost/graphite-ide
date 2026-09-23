#pragma once

#include "Sessions/Pricing.h"
#include "Sessions/TranscriptRecord.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Graphite {

	// Adds a request's usage once, however many records carry it. Claude Code writes one
	// record per content block of a response and repeats the response's usage on each, so a
	// sum over records counts one request two to five times.
	class UsageTally
	{
	public:
		void Add(const TranscriptRecord& record);

		const UsageByModel& Get() const { return m_Usage; }

	private:
		std::unordered_set<std::string> m_Seen;
		UsageByModel m_Usage;
	};

	// What says a session changed since it was last summarised: the newest write time and
	// total size across the session file and its subagents, and how many files there are.
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

		friend bool operator==(const SessionSummary&, const SessionSummary&) = default;
	};

	SessionSummary SummariseSession(const std::filesystem::path& sessionFile);

	std::optional<std::chrono::sys_time<std::chrono::milliseconds>> ParseTimestamp(std::string_view text);

	// Whether `inner` is `outer` or a directory below it, compared as Windows compares paths:
	// without regard to case or to a trailing separator.
	bool IsWithinDirectory(const std::filesystem::path& inner, const std::filesystem::path& outer);

	// At most `maxBytes` bytes, cut back to the start of a character so the result is still
	// UTF-8. ImGui draws a split character as a replacement glyph.
	std::string TruncateUtf8(std::string_view text, std::size_t maxBytes);
}
