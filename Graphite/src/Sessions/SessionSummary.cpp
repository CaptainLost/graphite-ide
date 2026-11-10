#include "Sessions/SessionSummary.h"

#include "Core/Base/Ascii.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/Encoding.h"

#include "Sessions/Transcript/SubagentLinks.h"
#include "Sessions/Transcript/TranscriptReader.h"

#include <algorithm>
#include <charconv>
#include <functional>

namespace Graphite {

	namespace {
		constexpr std::size_t kPromptBytes = 160;
		constexpr std::uint64_t kReadFromStart = 0;

		constexpr std::size_t kMinimumTimestampLength = 19;
		constexpr std::size_t kTimestampWithMillisecondsLength = 24;

		constexpr std::size_t kYearStart = 0;
		constexpr std::size_t kYearWidth = 4;
		constexpr std::size_t kMonthStart = 5;
		constexpr std::size_t kDayStart = 8;
		constexpr std::size_t kHourStart = 11;
		constexpr std::size_t kMinuteStart = 14;
		constexpr std::size_t kSecondStart = 17;
		constexpr std::size_t kMillisecondStart = 20;
		constexpr std::size_t kMillisecondWidth = 3;
		constexpr std::size_t kTwoDigitWidth = 2;

		constexpr std::size_t kYearMonthSeparatorIndex = 4;
		constexpr std::size_t kMonthDaySeparatorIndex = 7;
		constexpr std::size_t kDateTimeSeparatorIndex = 10;
		constexpr std::size_t kHourMinuteSeparatorIndex = 13;
		constexpr std::size_t kMinuteSecondSeparatorIndex = 16;
		constexpr std::size_t kMillisecondSeparatorIndex = 19;

		void AddFile(Fingerprint& fingerprint, const std::filesystem::path& file)
		{
			std::error_code fileStatus;
			const std::uintmax_t fileSize = std::filesystem::file_size(file, fileStatus);
			if (fileStatus)
			{
				return;
			}

			const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(file, fileStatus);
			if (fileStatus)
			{
				return;
			}

			fingerprint.Bytes += fileSize;
			fingerprint.WriteTicks =
				std::max<std::int64_t>(fingerprint.WriteTicks, lastWriteTime.time_since_epoch().count());
			++fingerprint.Files;
		}


		bool ParseField(std::string_view text, std::size_t fieldStart, std::size_t fieldWidth, int& value)
		{
			if (fieldStart + fieldWidth > text.size())
			{
				return false;
			}

			const std::string_view field = text.substr(fieldStart, fieldWidth);
			const std::from_chars_result parsed = std::from_chars(field.data(), field.data() + field.size(), value);
			return parsed.ec == std::errc{} && parsed.ptr == field.data() + field.size();
		}

		bool HasTimestampSeparators(std::string_view text)
		{
			return text[kYearMonthSeparatorIndex] == '-' && text[kMonthDaySeparatorIndex] == '-' &&
				   text[kDateTimeSeparatorIndex] == 'T' && text[kHourMinuteSeparatorIndex] == ':' &&
				   text[kMinuteSecondSeparatorIndex] == ':';
		}

		bool HasAnyTokens(const UsageByModel& usage)
		{
			for ([[maybe_unused]] const auto& [usageKey, counts] : usage)
			{
				if (counts.Total() != 0)
				{
					return true;
				}
			}

			return false;
		}

		void AddSubagentUsage(SessionSummary& summary, const std::filesystem::path& subagentFile)
		{
			++summary.SubagentCount;
			UsageTally subagentTally;
			ActivityRecorder subagentActivity{TranscriptThread::Subagent};

			if (Core::Result<TranscriptRead> subagentRead = ReadTranscript(subagentFile, kReadFromStart))
			{
				for (const TranscriptRecord& record : subagentRead.GetValue().Chunk.Records)
				{
					subagentTally.Add(record);
					subagentActivity.Add(record);
				}
			}

			subagentActivity.AddTo(summary.Activity);

			for (const auto& [usageKey, counts] : subagentTally.Get())
			{
				summary.Subagents[usageKey] += counts;
			}
		}
	}

	void UsageTally::Add(const TranscriptRecord& record)
	{
		if (!record.Usage.has_value())
		{
			return;
		}

		const std::string& requestKey = record.RequestId.empty() ? record.Uuid : record.RequestId;
		const UsageKey usageKey{record.Model, record.Fast};

		if (requestKey.empty())
		{
			m_Usage[usageKey] += *record.Usage;
			return;
		}

		if (!m_Counted.contains(requestKey))
		{
			m_Counted.emplace(requestKey, Counted{usageKey, {}});
		}

		Counted& counted = m_Counted.at(requestKey);

		counted.Usage.Add(*record.Usage, &m_Usage[counted.Key]);
	}

	std::filesystem::path SubagentDirectory(const std::filesystem::path& sessionFile)
	{
		return sessionFile.parent_path() / sessionFile.stem() / "subagents";
	}

	Fingerprint FingerprintOf(const std::filesystem::path& sessionFile)
	{
		Fingerprint fingerprint;
		AddFile(fingerprint, sessionFile);
		ForEachSubagentTranscript(
			sessionFile, [&fingerprint](const std::filesystem::path& file, const std::string&) { AddFile(fingerprint, file); });
		return fingerprint;
	}

	std::string SessionSummary::DisplayTitle() const
	{
		if (!Title.empty())
		{
			return Title;
		}

		return FirstPrompt.empty() ? Id : FirstPrompt;
	}

	bool SessionSummary::IsEmpty() const
	{
		return Title.empty() && FirstPrompt.empty() && !HasAnyTokens(Main) && !HasAnyTokens(Subagents);
	}

	SessionSummary SummariseSession(const std::filesystem::path& sessionFile)
	{
		std::optional<SessionSummary> summary = TrySummariseSession(sessionFile);

		if (summary.has_value())
		{
			return std::move(*summary);
		}

		SessionSummary unreadableSummary;
		unreadableSummary.Id = Core::ToUtf8(sessionFile.stem());
		unreadableSummary.File = sessionFile;
		unreadableSummary.Print = FingerprintOf(sessionFile);
		return unreadableSummary;
	}

	std::optional<SessionSummary> TrySummariseSession(const std::filesystem::path& sessionFile)
	{
		// WARNING: The file is fingerprinted before it is read, so a line appended in between leaves the
		// fingerprint older than the summary and the next look summarises again, rather than the other way round.
		const Fingerprint fingerprint = FingerprintOf(sessionFile);
		Core::Result<TranscriptRead> transcriptRead = ReadTranscript(sessionFile, kReadFromStart);

		if (!transcriptRead)
		{
			return std::nullopt;
		}

		SessionSummary summary;
		summary.Id = Core::ToUtf8(sessionFile.stem());
		summary.File = sessionFile;
		summary.Print = fingerprint;
		summary.SkippedLines = transcriptRead.GetValue().Chunk.Skipped;

		UsageTally mainTally;
		ActivityRecorder mainActivity{TranscriptThread::Main};

		for (const TranscriptRecord& record : transcriptRead.GetValue().Chunk.Records)
		{
			mainTally.Add(record);
			mainActivity.Add(record);

			if (record.Kind == RecordKind::AiTitle && !record.Title.empty())
			{
				summary.Title = record.Title;
			}

			if (summary.Cwd.empty() && !record.Cwd.empty())
			{
				summary.Cwd = Core::FromUtf8(record.Cwd);
			}

			if (summary.FirstPrompt.empty())
			{
				if (std::optional<std::string> prompt = TypedPrompt(record))
				{
					summary.FirstPrompt = TruncateUtf8(*prompt, kPromptBytes);
				}
			}

			// WARNING: Timestamps are ISO 8601 in UTC, which compares as text in time order.
			if (!record.Timestamp.empty())
			{
				if (summary.StartedAt.empty() || record.Timestamp < summary.StartedAt)
				{
					summary.StartedAt = record.Timestamp;
				}
				if (record.Timestamp > summary.EndedAt)
				{
					summary.EndedAt = record.Timestamp;
				}
			}
		}

		summary.Main = mainTally.Get();
		mainActivity.AddTo(summary.Activity);

		ForEachSubagentTranscript(sessionFile, [&summary](const std::filesystem::path& subagentFile, const std::string&) { AddSubagentUsage(summary, subagentFile); });

		return summary;
	}

	bool SameSessionId(std::string_view firstId, std::string_view secondId)
	{
		return Core::EqualsIgnoringCase(firstId, secondId);
	}

	std::optional<std::chrono::sys_time<std::chrono::milliseconds>> ParseTimestamp(std::string_view text)
	{
		if (text.size() < kMinimumTimestampLength || !HasTimestampSeparators(text))
		{
			return std::nullopt;
		}

		int year = 0;
		int month = 0;
		int day = 0;
		int hour = 0;
		int minute = 0;
		int second = 0;
		int millisecond = 0;

		if (!ParseField(text, kYearStart, kYearWidth, year) || !ParseField(text, kMonthStart, kTwoDigitWidth, month) ||
			!ParseField(text, kDayStart, kTwoDigitWidth, day) || !ParseField(text, kHourStart, kTwoDigitWidth, hour) ||
			!ParseField(text, kMinuteStart, kTwoDigitWidth, minute) ||
			!ParseField(text, kSecondStart, kTwoDigitWidth, second))
		{
			return std::nullopt;
		}

		if (text.size() >= kTimestampWithMillisecondsLength && text[kMillisecondSeparatorIndex] == '.')
		{
			ParseField(text, kMillisecondStart, kMillisecondWidth, millisecond);
		}

		const std::chrono::year_month_day date{std::chrono::year{year},
											   std::chrono::month{static_cast<unsigned>(month)},
											   std::chrono::day{static_cast<unsigned>(day)}};
		if (!date.ok())
		{
			return std::nullopt;
		}

		return std::chrono::sys_days{date} + std::chrono::hours{hour} + std::chrono::minutes{minute} +
			   std::chrono::seconds{second} + std::chrono::milliseconds{millisecond};
	}

	bool IsWithinDirectory(const std::filesystem::path& inner, const std::filesystem::path& outer)
	{
		return !inner.empty() && Core::RelativeTo(inner, outer).has_value();
	}

	std::filesystem::path::string_type DirectoryKey(const std::filesystem::path& directory)
	{
		return Core::PathKey(directory);
	}

	std::string TruncateUtf8(std::string_view text, std::size_t maxBytes)
	{
		if (text.size() <= maxBytes)
		{
			return std::string{text};
		}

		return std::string{text.substr(0, Core::CharacterBoundaryAtOrBefore(text, maxBytes))};
	}
}
