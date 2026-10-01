#include "Sessions/LiveTranscript.h"

#include "Core/App/Redraw.h"
#include "Core/Fs/FileSystem.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/SubagentLinks.h"
#include "Sessions/TranscriptReader.h"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		// Small enough that a chunk parses in a moment even in a Debug build, so the reader
		// sees the conversation fill in and closing a tab waits for no more than one chunk.
		constexpr std::uint64_t kChunkBytes = 1024 * 1024;
	}

	bool LiveTranscript::ReadMain(const std::filesystem::path& file, Inbox& inbox)
	{
		std::uint64_t offset = 0;

		{
			const std::lock_guard<std::mutex> guard(inbox.Mutex);
			offset = inbox.Offset;
		}

		while (true)
		{
			// A missing or locked file leaves the offset where it was; the next poll retries.
			// The file is read outside the lock, so the UI never waits on the disk.
			Core::Result<TranscriptRead> read = ReadTranscriptChunk(file, offset, kChunkBytes);

			const std::lock_guard<std::mutex> guard(inbox.Mutex);

			if (!read.HasValue())
				return !inbox.Stopping;

			TranscriptRead& value = read.GetValue();

			if (value.Restarted)
			{
				inbox.Records.clear();
				inbox.Restarted = true;
				inbox.Skipped = 0;
			}

			for (TranscriptRecord& record : value.Chunk.Records)
				inbox.Records.push_back(std::move(record));

			inbox.Skipped += value.Chunk.Skipped;
			inbox.Offset = value.NextOffset;
			inbox.Progress.Bytes = value.NextOffset;
			inbox.Progress.Total = value.FileSize;

			// Per chunk, so a long transcript fills in and its progress bar moves as it loads.
			Core::Redraw::Request();

			if (inbox.Stopping)
				return false;

			// Done at the end of the file, or at a last line still being written, which a
			// chunk leaves unread and the next poll picks up.
			if (value.NextOffset >= value.FileSize || (value.NextOffset == offset && !value.Restarted))
				return true;

			offset = value.NextOffset;
		}
	}

	std::optional<LiveTranscript::SubagentState> LiveTranscript::FollowSubagents(
		const std::filesystem::path& sessionFile, SubagentFiles& subagents, Inbox& inbox)
	{
		std::map<std::filesystem::path, SubagentFile>& files = subagents.Files;
		bool moved = false;
		std::vector<std::filesystem::path> present;
		std::error_code status;

		// Listed before any is read, so the progress knows how many there are.
		for (std::filesystem::directory_iterator it{SubagentDirectory(sessionFile), status}, end;
			 !status && it != end; it.increment(status))
		{
			const std::filesystem::path& path = it->path();

			if (path.extension() == ".jsonl" && Core::ToUtf8(path.stem()).starts_with("agent-"))
				present.push_back(path);
		}

		// Counted through only on the first walk. A later one reads just what moved, in a
		// moment, and counting it through would flash the view's "reading subagents" line
		// on every poll of a live session.
		bool counting = false;

		{
			const std::lock_guard<std::mutex> guard(inbox.Mutex);
			counting = !inbox.Progress.SubagentsWalked;

			if (counting)
			{
				inbox.Progress.SubagentsDone = 0;
				inbox.Progress.SubagentsTotal = present.size();
			}
		}

		std::set<std::filesystem::path> seen;

		for (std::size_t index = 0; index < present.size(); ++index)
		{
			// Checked before every file rather than once per walk: a closing tab must not wait
			// for dozens of them.
			{
				const std::lock_guard<std::mutex> guard(inbox.Mutex);

				if (inbox.Stopping)
					return std::nullopt;

				if (counting)
					inbox.Progress.SubagentsDone = index;
			}

			Core::Redraw::Request();

			const std::filesystem::path& path = present[index];

			std::error_code sizeStatus;
			const std::uintmax_t size = std::filesystem::file_size(path, sizeStatus);
			if (sizeStatus)
				continue;

			seen.insert(path);

			const auto [found, added] = files.try_emplace(path);
			SubagentFile& file = found->second;
			moved = moved || added;

			if (!file.Link)
			{
				file.Link = ReadSubagentMeta(path);
				moved = moved || file.Link.has_value();
			}

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

		{
			const std::lock_guard<std::mutex> guard(inbox.Mutex);
			inbox.Progress.SubagentsDone = present.size();
			inbox.Progress.SubagentsTotal = present.size();
		}

		// Files not seen have gone, but only when the walk saw the whole folder: one cut short
		// by an error has not seen them, and forgetting them would read them again from zero.
		const bool walked = !status || status == std::errc::no_such_file_or_directory;

		for (auto it = files.begin(); walked && it != files.end();)
		{
			if (seen.contains(it->first))
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

		SubagentState state;
		for (const auto& [path, file] : files)
		{
			state.Usage[Core::ToUtf8(path.stem()).substr(6)] = file.Tally.Get();

			if (file.Link)
				state.Links.insert(*file.Link);
		}

		return state;
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
			// However the read ends, what it published last has to be drawn.
			struct WakeOnExit
			{
				~WakeOnExit() { Core::Redraw::Request(); }
			} wakeOnExit;

			while (true)
			{
				{
					const std::lock_guard<std::mutex> guard(inbox->Mutex);

					if (inbox->Stopping)
					{
						inbox->Reading = false;
						return;
					}

					inbox->Again = false;
				}

				// The conversation is published before the subagents are looked at, so a
				// subagent's write never holds back what the main transcript already said.
				const bool going = ReadMain(file, *inbox);

				{
					const std::lock_guard<std::mutex> guard(inbox->Mutex);

					// Ended even when the file could not be read: a view waiting for the first
					// pass would otherwise wait on a file that may never appear.
					inbox->Progress.FirstPassDone = true;

					if (!going)
					{
						inbox->Reading = false;
						return;
					}
				}

				std::optional<SubagentState> subagents = FollowSubagents(file, *subagentFiles, *inbox);

				const std::lock_guard<std::mutex> guard(inbox->Mutex);

				if (subagents.has_value())
					inbox->Subagents = std::move(*subagents);

				// Marked with the usage it produced, so whoever sees the walk done can take it.
				// A walk cut short by closing is not done.
				if (!inbox->Stopping)
					inbox->Progress.SubagentsWalked = true;

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
		return TakeRecords(session, nullptr);
	}

	bool LiveTranscript::Take(Session& session, RequestTimeline& timeline)
	{
		return TakeRecords(session, &timeline);
	}

	bool LiveTranscript::TakeRecords(Session& session, RequestTimeline* timeline)
	{
		std::vector<TranscriptRecord> records;
		bool restarted = false;

		{
			const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
			records.swap(m_Inbox->Records);
			restarted = std::exchange(m_Inbox->Restarted, false);
		}

		if (restarted)
		{
			session.Reset();
			if (timeline != nullptr)
				timeline->Reset();
		}

		session.Append(records);
		if (timeline != nullptr)
			timeline->Append(records);

		return restarted || !records.empty();
	}

	bool LiveTranscript::TakeSubagents(SubagentUsage& usage, SubagentLinks& links)
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);

		if (!m_Inbox->Subagents.has_value())
			return false;

		usage = std::move(m_Inbox->Subagents->Usage);
		links = std::move(m_Inbox->Subagents->Links);
		m_Inbox->Subagents.reset();
		return true;
	}

	LiveTranscript::LoadProgress LiveTranscript::GetProgress() const
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
		return m_Inbox->Progress;
	}

	std::size_t LiveTranscript::GetSkipped() const
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);
		return m_Inbox->Skipped;
	}
}
