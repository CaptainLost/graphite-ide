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

	// The summary, or nothing when the session file itself could not be read. A summary of an
	// unreadable file would be an empty session with a valid fingerprint, and a caller that
	// remembers it would never look at the file again once it becomes readable.
	std::optional<SessionSummary> TrySummariseSession(const std::filesystem::path& sessionFile);

	// Claude Code writes session ids in lower case, and a UUID formatted elsewhere may not be;
	// they name the same session either way.
	bool SameSessionId(std::string_view a, std::string_view b);

	std::optional<std::chrono::sys_time<std::chrono::milliseconds>> ParseTimestamp(std::string_view text);

	// Whether `inner` is `outer` or a directory below it, compared as Windows compares paths:
	// without regard to case or to a trailing separator.
	bool IsWithinDirectory(const std::filesystem::path& inner, const std::filesystem::path& outer);

	// The form IsWithinDirectory compares in: two folders are the same folder exactly when
	// their keys are equal. Empty for an empty path.
	std::wstring DirectoryKey(const std::filesystem::path& directory);

	// At most `maxBytes` bytes, cut back to the start of a character so the result is still
	// UTF-8. ImGui draws a split character as a replacement glyph.
	std::string TruncateUtf8(std::string_view text, std::size_t maxBytes);
}
