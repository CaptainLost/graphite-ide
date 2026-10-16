#include "Sessions/Transcript/LiveTranscript.h"

#include "Core/App/Redraw.h"
#include "Core/Base/ScopeExit.h"
#include "Core/FileSystem/FileSystem.h"

#include "Sessions/SessionSummary.h"
#include "Sessions/Transcript/SubagentLinks.h"
#include "Sessions/Transcript/TranscriptReader.h"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
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
			// WARNING: The file is read before the lock is taken, so the UI never waits on the disk.
			Core::Result<TranscriptRead> chunkRead = ReadTranscriptChunk(file, offset, kChunkBytes);

			const std::lock_guard<std::mutex> guard(inbox.Mutex);

			if (!chunkRead.HasValue())
			{
				return !inbox.Stopping;
			}

			TranscriptRead& transcriptRead = chunkRead.GetValue();

			if (transcriptRead.Restarted)
			{
				inbox.Records.clear();
				inbox.Restarted = true;
				inbox.Skipped = 0;
			}

			for (TranscriptRecord& record : transcriptRead.Chunk.Records)
			{
				inbox.Records.push_back(std::move(record));
			}

			inbox.Skipped += transcriptRead.Chunk.Skipped;
			inbox.Offset = transcriptRead.NextOffset;
			inbox.Progress.Bytes = transcriptRead.NextOffset;
			inbox.Progress.Total = transcriptRead.FileSize;

			Core::Redraw::Request();

			if (inbox.Stopping)
			{
				return false;
			}

			const bool reachedEndOfFile = transcriptRead.NextOffset >= transcriptRead.FileSize;
			const bool stoppedAtUnfinishedLine = transcriptRead.NextOffset == offset && !transcriptRead.Restarted;
			if (reachedEndOfFile || stoppedAtUnfinishedLine)
			{
				return true;
			}

			offset = transcriptRead.NextOffset;
		}
	}

	std::optional<LiveTranscript::SubagentState> LiveTranscript::FollowSubagents(
		const std::filesystem::path& sessionFile, SubagentFiles& subagents, Inbox& inbox)
	{
		using SubagentFileMap = std::map<std::filesystem::path, SubagentFile>;

		SubagentFileMap& files = subagents.Files;
		bool anythingMoved = false;
		std::vector<std::filesystem::path> presentFiles;
		const bool sawWholeFolder =
			ForEachSubagentTranscript(sessionFile, [&presentFiles](const std::filesystem::path& transcript, const std::string&) {
				presentFiles.push_back(transcript);
			});

		// WARNING: Progress is counted only on the first walk. A later walk reads just what moved,
		// in a moment, and counting it would flash the view's "reading subagents" line on every poll.
		bool countingProgress = false;

		{
			const std::lock_guard<std::mutex> guard(inbox.Mutex);
			countingProgress = !inbox.Progress.SubagentsWalked;

			if (countingProgress)
			{
				inbox.Progress.SubagentsDone = 0;
				inbox.Progress.SubagentsTotal = presentFiles.size();
			}
		}

		std::set<std::filesystem::path> seenFiles;

		for (std::size_t presentIndex = 0; presentIndex < presentFiles.size(); ++presentIndex)
		{
			// WARNING: Stopping is checked before every file, not once per walk, so a closing tab
			// does not wait for dozens of them.
			{
				const std::lock_guard<std::mutex> guard(inbox.Mutex);

				if (inbox.Stopping)
				{
					return std::nullopt;
				}

				if (countingProgress)
				{
					inbox.Progress.SubagentsDone = presentIndex;
				}
			}

			Core::Redraw::Request();

			const std::filesystem::path& subagentPath = presentFiles[presentIndex];

			std::error_code fileSizeStatus;
			const std::uintmax_t fileSize = std::filesystem::file_size(subagentPath, fileSizeStatus);
			if (fileSizeStatus)
			{
				continue;
			}

			seenFiles.insert(subagentPath);

			const auto [entryPosition, wasAdded] = files.try_emplace(subagentPath);
			auto& [placedPath, subagentFile] = *entryPosition;
			anythingMoved = anythingMoved || wasAdded;

			if (!subagentFile.Link)
			{
				subagentFile.Link = ReadSubagentMeta(subagentPath);
				anythingMoved = anythingMoved || subagentFile.Link.has_value();
			}

			if (!wasAdded && fileSize == subagentFile.Size)
			{
				continue;
			}

			Core::Result<TranscriptRead> transcriptRead = ReadTranscript(subagentPath, subagentFile.Offset);
			if (!transcriptRead)
			{
				continue;
			}

			TranscriptRead& readValue = transcriptRead.GetValue();

			if (readValue.Restarted)
			{
				subagentFile.Tally = UsageTally{};
				anythingMoved = true;
			}

			for (const TranscriptRecord& record : readValue.Chunk.Records)
			{
				subagentFile.Tally.Add(record);
			}

			anythingMoved = anythingMoved || !readValue.Chunk.Records.empty();
			subagentFile.Offset = readValue.NextOffset;
			subagentFile.Size = fileSize;
		}

		{
			const std::lock_guard<std::mutex> guard(inbox.Mutex);
			inbox.Progress.SubagentsDone = presentFiles.size();
			inbox.Progress.SubagentsTotal = presentFiles.size();
		}

		// WARNING: Unseen files are forgotten only when the walk saw the whole folder. A walk cut
		// short by an error has not seen them, and forgetting them would read them again from zero.
		std::vector<std::filesystem::path> goneFiles;

		if (sawWholeFolder)
		{
			for (const auto& [knownPath, knownFile] : files)
			{
				if (!seenFiles.contains(knownPath))
				{
					goneFiles.push_back(knownPath);
				}
			}
		}

		for (const std::filesystem::path& gonePath : goneFiles)
		{
			files.erase(gonePath);
			anythingMoved = true;
		}

		if (!anythingMoved)
		{
			return std::nullopt;
		}

		SubagentState state;
		for (const auto& [subagentPath, subagentFile] : files)
		{
			if (const std::optional<std::string> agentId = SubagentIdOf(subagentPath))
			{
				state.Usage[*agentId] = subagentFile.Tally.Get();
			}

			if (subagentFile.Link)
			{
				state.Links.insert(*subagentFile.Link);
			}
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

		m_Jobs.Submit([inbox = m_Inbox, subagentFiles = m_Subagents, file = m_File] {
			const Core::ScopeExit wakeOnExit{[] { Core::Redraw::Request(); }};

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

				// WARNING: The conversation is published before the subagents are looked at, so a
				// subagent's write never holds back what the main transcript already said.
				const bool mainTranscriptOpen = ReadMain(file, *inbox);

				{
					const std::lock_guard<std::mutex> guard(inbox->Mutex);

					inbox->Progress.FirstPassDone = true;

					if (!mainTranscriptOpen)
					{
						inbox->Reading = false;
						return;
					}
				}

				std::optional<SubagentState> followedSubagents = FollowSubagents(file, *subagentFiles, *inbox);

				const std::lock_guard<std::mutex> guard(inbox->Mutex);

				if (followedSubagents.has_value())
				{
					inbox->Subagents = std::move(*followedSubagents);
				}

				if (!inbox->Stopping)
				{
					inbox->Progress.SubagentsWalked = true;
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
			constexpr bool kRestartHandled = false;
			restarted = std::exchange(m_Inbox->Restarted, kRestartHandled);
		}

		if (restarted)
		{
			session.Reset();
			if (timeline != nullptr)
			{
				timeline->Reset();
			}
		}

		session.Append(records);
		if (timeline != nullptr)
		{
			timeline->Append(records);
		}

		return restarted || !records.empty();
	}

	bool LiveTranscript::TakeSubagents(SubagentUsage& usage, SubagentLinks& links)
	{
		const std::lock_guard<std::mutex> guard(m_Inbox->Mutex);

		if (!m_Inbox->Subagents.has_value())
		{
			return false;
		}

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
