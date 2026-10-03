#include "Sessions/SessionSummary.h"

#include "Core/Base/Ascii.h"
#include "Core/Fs/FileSystem.h"

#include "Sessions/InjectedText.h"
#include "Sessions/TranscriptReader.h"

#include <algorithm>
#include <charconv>

namespace Graphite {

	namespace {
		constexpr std::size_t kPromptBytes = 160;

		// A typed prompt, as opposed to text Claude Code put in the user's mouth: an injected
		// skill (IsMeta) or a notification, command echo or reminder recognised by its tag.
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

		void AddFile(Fingerprint& print, const std::filesystem::path& file)
		{
			std::error_code status;
			const auto size = std::filesystem::file_size(file, status);
			if (status)
			{
				return;
			}

			const auto written = std::filesystem::last_write_time(file, status);
			if (status)
			{
				return;
			}

			print.Bytes += size;
			print.WriteTicks = std::max<std::int64_t>(print.WriteTicks, written.time_since_epoch().count());
			++print.Files;
		}

		template<std::invocable<const std::filesystem::path&> Visit>
		void ForEachSubagent(const std::filesystem::path& sessionFile, Visit visit)
		{
			std::error_code status;
			const std::filesystem::path directory = SubagentDirectory(sessionFile);

			for (std::filesystem::directory_iterator it{directory, status}, end; !status && it != end;
				 it.increment(status))
			{
				if (it->path().extension() == ".jsonl")
				{
					visit(it->path());
				}
			}
		}

		// Parses a fixed-width field at `start`..`start+width` into `value`; returns false if
		// the field is not entirely digits of that width.
		bool ParseField(std::string_view text, std::size_t start, std::size_t width, int& value)
		{
			if (start + width > text.size())
			{
				return false;
			}

			const std::string_view field = text.substr(start, width);
			const auto [next, error] = std::from_chars(field.data(), field.data() + field.size(), value);
			return error == std::errc{} && next == field.data() + field.size();
		}
	}

	void UsageTally::Add(const TranscriptRecord& record)
	{
		if (!record.Usage.has_value())
		{
			return;
		}

		// A record with no request id is its own request; its uuid keeps it from being
		// merged with another such record.
		const std::string& key = record.RequestId.empty() ? record.Uuid : record.RequestId;
		const UsageKey model{record.Model, record.Fast};

		if (key.empty())
		{
			m_Usage[model] += *record.Usage;
			return;
		}

		Counted& counted = m_Counted.try_emplace(key, Counted{model, {}}).first->second;

		// The first record names the model, and the rest only revise the counts it put there.
		counted.Usage.Add(*record.Usage, &m_Usage[counted.Key]);
	}

	std::filesystem::path SubagentDirectory(const std::filesystem::path& sessionFile)
	{
		return sessionFile.parent_path() / sessionFile.stem() / "subagents";
	}

	Fingerprint FingerprintOf(const std::filesystem::path& sessionFile)
	{
		Fingerprint print;
		AddFile(print, sessionFile);
		ForEachSubagent(sessionFile, [&print](const std::filesystem::path& file) { AddFile(print, file); });
		return print;
	}

	std::string SessionSummary::DisplayTitle() const
	{
		if (!Title.empty())
		{
			return Title;
		}

		return FirstPrompt.empty() ? Id : FirstPrompt;
	}

	SessionSummary SummariseSession(const std::filesystem::path& sessionFile)
	{
		std::optional<SessionSummary> summary = TrySummariseSession(sessionFile);

		if (summary.has_value())
		{
			return std::move(*summary);
		}

		// What the callers of this form have always had for a file that could not be read:
		// the session's identity and fingerprint, and nothing else.
		SessionSummary empty;
		empty.Id = Core::ToUtf8(sessionFile.stem());
		empty.File = sessionFile;
		empty.Print = FingerprintOf(sessionFile);
		return empty;
	}

	std::optional<SessionSummary> TrySummariseSession(const std::filesystem::path& sessionFile)
	{
		// Fingerprinted before the read, so a line appended in between leaves the print older
		// than the summary and the next look summarises again, rather than the other way round.
		const Fingerprint print = FingerprintOf(sessionFile);
		Core::Result<TranscriptRead> read = ReadTranscript(sessionFile, 0);

		if (!read)
		{
			return std::nullopt;
		}

		SessionSummary summary;
		summary.Id = Core::ToUtf8(sessionFile.stem());
		summary.File = sessionFile;
		summary.Print = print;
		summary.SkippedLines = read.GetValue().Chunk.Skipped;

		UsageTally main;

		for (const TranscriptRecord& record : read.GetValue().Chunk.Records)
		{
			main.Add(record);

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

			// ISO 8601 in UTC compares as text in time order.
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

		summary.Main = main.Get();

		ForEachSubagent(sessionFile, [&summary](const std::filesystem::path& file) {
			++summary.SubagentCount;
			UsageTally tally;

			if (Core::Result<TranscriptRead> agent = ReadTranscript(file, 0))
			{
				for (const TranscriptRecord& record : agent.GetValue().Chunk.Records)
				{
					tally.Add(record);
				}
			}

			for (const auto& [key, counts] : tally.Get())
			{
				summary.Subagents[key] += counts;
			}
		});

		return summary;
	}

	bool SameSessionId(std::string_view a, std::string_view b)
	{
		return Core::EqualsIgnoringCase(a, b);
	}

	std::optional<std::chrono::sys_time<std::chrono::milliseconds>> ParseTimestamp(std::string_view text)
	{
		// "2026-09-17T09:29:50.246Z": fixed positions for year, month, day, hour, minute,
		// second and millisecond, with the separators checked so a malformed string is
		// rejected rather than mis-parsed.
		if (text.size() < 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
			text[16] != ':')
		{
			return std::nullopt;
		}

		int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millisecond = 0;

		if (!ParseField(text, 0, 4, year) || !ParseField(text, 5, 2, month) || !ParseField(text, 8, 2, day) ||
			!ParseField(text, 11, 2, hour) || !ParseField(text, 14, 2, minute) ||
			!ParseField(text, 17, 2, second))
		{
			return std::nullopt;
		}

		if (text.size() >= 24 && text[19] == '.')
		{
			ParseField(text, 20, 3, millisecond);
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

		std::size_t cut = maxBytes;

		// Continuation bytes are 10xxxxxx; back off until the cut lands before a lead byte.
		while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0u) == 0x80u)
		{
			--cut;
		}

		return std::string{text.substr(0, cut)};
	}
}
