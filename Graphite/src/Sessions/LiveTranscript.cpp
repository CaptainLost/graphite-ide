#include "Sessions/LiveTranscript.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/TranscriptReader.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace Graphite {

	namespace {
		// The subagents' folder, walked the same way FingerprintOf walks it: file count, total
		// size and newest write time, so a change to any of the three means "read it again".
		struct SubagentFingerprint
		{
			std::uintmax_t Bytes = 0;
			std::int64_t Ticks = 0;
			std::size_t Files = 0;
		};

		SubagentFingerprint FingerprintSubagents(const std::filesystem::path& sessionFile)
		{
			SubagentFingerprint print;
			std::error_code status;

			for (std::filesystem::directory_iterator it{SubagentDirectory(sessionFile), status}, end;
				 !status && it != end; it.increment(status))
			{
				const std::string stem = it->path().stem().string();

				if (it->path().extension() != ".jsonl" || !stem.starts_with("agent-"))
					continue;

				std::error_code fileStatus;
				const auto size = std::filesystem::file_size(it->path(), fileStatus);
				if (fileStatus)
					continue;

				const auto written = std::filesystem::last_write_time(it->path(), fileStatus);
				if (fileStatus)
					continue;

				print.Bytes += size;
				print.Ticks = std::max<std::int64_t>(print.Ticks, written.time_since_epoch().count());
				++print.Files;
			}

			return print;
		}
	}

	LiveTranscript::LiveTranscript(std::filesystem::path file) : m_File(std::move(file)) {}

	LiveTranscript::~LiveTranscript()
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
		m_Inbox->Stopping = true;
	}

	void LiveTranscript::Poll()
	{
		{
			const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);

			if (m_Inbox->Reading)
			{
				m_Inbox->Again = true;
				return;
			}

			m_Inbox->Reading = true;
		}

		m_Jobs.Submit([inbox = m_Inbox, file = m_File]
		{
			while (true)
			{
				std::uint64_t offset = 0;

				{
					const std::lock_guard<std::mutex> guard(inbox->Mutex);

					if (inbox->Stopping)
					{
						inbox->Reading = false;
						return;
					}

					inbox->Again = false;
					offset = inbox->Offset;
				}

				// A missing or locked file leaves the offset where it was; the next poll retries.
				Core::Result<TranscriptRead> read = ReadTranscript(file, offset);

				// Same walk FingerprintOf uses, over the subagents' folder alone; a change in
				// any of the three means the folder is worth reading again.
				const SubagentFingerprint subagentPrint = FingerprintSubagents(file);
				bool subagentsChanged = false;

				{
					const std::lock_guard<std::mutex> guard(inbox->Mutex);
					subagentsChanged = subagentPrint.Files != inbox->SubagentFiles ||
										subagentPrint.Bytes != inbox->SubagentBytes ||
										subagentPrint.Ticks != inbox->SubagentTicks;
				}

				std::optional<SubagentUsage> subagents;
				if (subagentsChanged)
					subagents = LoadSubagentUsage(file);

				const std::lock_guard<std::mutex> guard(inbox->Mutex);

				if (read.HasValue())
				{
					TranscriptRead& value = read.GetValue();

					if (value.Restarted)
					{
						inbox->Records.clear();
						inbox->Restarted = true;
						inbox->Skipped = 0;
					}

					for (TranscriptRecord& record : value.Chunk.Records)
						inbox->Records.push_back(std::move(record));

					inbox->Skipped += value.Chunk.Skipped;
					inbox->Offset = value.NextOffset;
				}

				if (subagents.has_value())
				{
					inbox->Subagents = std::move(*subagents);
					inbox->SubagentBytes = subagentPrint.Bytes;
					inbox->SubagentTicks = subagentPrint.Ticks;
					inbox->SubagentFiles = subagentPrint.Files;
				}

				if (!inbox->Again)
				{
					inbox->Reading = false;
					return;
				}
			}
		});
	}

	bool LiveTranscript::Take(Session& session)
	{
		std::vector<TranscriptRecord> records;
		bool restarted = false;

		{
			const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
			records.swap(m_Inbox->Records);
			restarted = std::exchange(m_Inbox->Restarted, false);
		}

		if (restarted)
			session.Reset();

		session.Append(records);
		return restarted || !records.empty();
	}

	bool LiveTranscript::TakeSubagents(SubagentUsage& out)
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);

		if (!m_Inbox->Subagents.has_value())
			return false;

		out = std::move(*m_Inbox->Subagents);
		m_Inbox->Subagents.reset();
		return true;
	}

	std::size_t LiveTranscript::GetSkipped() const
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
		return m_Inbox->Skipped;
	}
}
