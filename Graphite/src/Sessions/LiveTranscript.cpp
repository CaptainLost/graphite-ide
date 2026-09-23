#include "Sessions/LiveTranscript.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/TranscriptReader.h"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace Graphite {

	std::optional<SubagentUsage> LiveTranscript::FollowSubagents(const std::filesystem::path& sessionFile,
																 SubagentFiles& subagents)
	{
		std::map<std::filesystem::path, SubagentFile>& files = subagents.Files;
		bool moved = false;
		std::set<std::filesystem::path> present;
		std::error_code status;

		for (std::filesystem::directory_iterator it{SubagentDirectory(sessionFile), status}, end;
			 !status && it != end; it.increment(status))
		{
			const std::filesystem::path& path = it->path();

			if (path.extension() != ".jsonl" || !path.stem().string().starts_with("agent-"))
				continue;

			std::error_code sizeStatus;
			const std::uintmax_t size = std::filesystem::file_size(path, sizeStatus);
			if (sizeStatus)
				continue;

			present.insert(path);

			const auto [found, added] = files.try_emplace(path);
			SubagentFile& file = found->second;
			moved = moved || added;

			if (!added && size == file.Size)
				continue;

			// A missing or locked file keeps what was read of it; the next poll retries.
			Core::Result<TranscriptRead> read = ReadTranscript(path, file.Offset);
			if (!read)
				continue;

			TranscriptRead& value = read.GetValue();

			// Shorter than what was read: rewritten, so its usage is counted afresh.
			if (value.Restarted)
			{
				file.Tally = {};
				moved = true;
			}

			for (const TranscriptRecord& record : value.Chunk.Records)
				file.Tally.Add(record);

			moved = moved || !value.Chunk.Records.empty();
			file.Offset = value.NextOffset;
			file.Size = size;
		}

		// Files not seen have gone, but only when the walk saw the whole folder: one cut short
		// by an error has not seen them, and forgetting them would read them again from zero.
		const bool walked = !status || status == std::errc::no_such_file_or_directory;

		for (auto it = files.begin(); walked && it != files.end();)
		{
			if (present.contains(it->first))
			{
				++it;
			}
			else
			{
				it = files.erase(it);
				moved = true;
			}
		}

		if (!moved)
			return std::nullopt;

		SubagentUsage usage;
		for (const auto& [path, file] : files)
			usage[path.stem().string().substr(6)] = file.Tally.Get();

		return usage;
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

		m_Jobs.Submit([inbox = m_Inbox, subagentFiles = m_Subagents, file = m_File]
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

				// The conversation is published before the subagents are looked at, so a
				// subagent's write never holds back what the main transcript already said.
				if (read.HasValue())
				{
					TranscriptRead& value = read.GetValue();
					const std::lock_guard<std::mutex> guard(inbox->Mutex);

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

				std::optional<SubagentUsage> subagents = FollowSubagents(file, *subagentFiles);

				const std::lock_guard<std::mutex> guard(inbox->Mutex);

				if (subagents.has_value())
					inbox->Subagents = std::move(*subagents);

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
