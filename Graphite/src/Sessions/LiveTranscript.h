#pragma once

#include "Sessions/RequestTimeline.h"
#include "Sessions/Session.h"
#include "Sessions/SessionSummary.h"
#include "Sessions/SubagentLinks.h"
#include "Sessions/ToolFacts.h"
#include "Sessions/TranscriptRecord.h"

#include "Core/Jobs/JobQueue.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Graphite {

	// Follows one transcript on a worker: each poll reads from where the last read stopped.
	// The file may not exist yet — a session started from the IDE has an id before Claude has
	// written a line — and a poll then finds nothing and the next one tries again.
	class LiveTranscript
	{
	public:
		explicit LiveTranscript(std::filesystem::path file);
		~LiveTranscript();

		LiveTranscript(const LiveTranscript&) = delete;
		LiveTranscript& operator=(const LiveTranscript&) = delete;

		// Asks for whatever is new. One read runs at a time; a poll during a read makes that
		// read go round once more instead of queueing another.
		void Poll();

		// UI thread only. Applies what arrived since the last call; true when anything did.
		bool Take(Session& session);

		// UI thread only. As above, and feeds the same records to a request timeline, resetting
		// it alongside the session on a restart.
		bool Take(Session& session, RequestTimeline& timeline);

		// UI thread only. Moves out the subagents' usage and links when a newer pair arrived
		// since the last call; true when it did. Both are replaced together.
		bool TakeSubagents(SubagentUsage& usage, SubagentLinks& links);

		// How far loading has got, for a view to show while there is little or nothing to draw.
		struct LoadProgress
		{
			// Of the main transcript: how far it has been read, and how long it was then.
			std::uint64_t Bytes = 0;
			std::uint64_t Total = 0;

			// Of the subagent transcripts in the latest look at them.
			std::size_t SubagentsDone = 0;
			std::size_t SubagentsTotal = 0;

			// A walk has listed the subagent folder and read every file that changed. Until
			// then the two counts above may still be the zeroes they start as, so "done equals
			// total" says nothing on its own.
			bool SubagentsWalked = false;

			// The first pass over the main transcript has ended, the subagents aside: from then
			// on the conversation is as complete as the file was when it started.
			bool FirstPassDone = false;
		};

		// UI thread only.
		LoadProgress GetProgress() const;

		std::size_t GetSkipped() const;
		const std::filesystem::path& GetFile() const { return m_File; }

	private:
		// What a walk of the subagent folder found: each subagent's usage by agent id, and
		// each linked subagent by the id of the call that started it.
		struct SubagentState
		{
			SubagentUsage Usage;
			SubagentLinks Links;
		};

		struct Inbox
		{
			std::mutex Mutex;
			std::vector<TranscriptRecord> Records;
			bool Restarted = false;
			std::size_t Skipped = 0;
			bool Reading = false;
			bool Again = false;
			bool Stopping = false;

			// Where the next read starts. Only the worker moves it.
			std::uint64_t Offset = 0;

			LoadProgress Progress;

			// What the subagent folder held, set when some subagent's transcript or meta file
			// moved on since the last pair and moved out by TakeSubagents.
			std::optional<SubagentState> Subagents;
		};

		// One subagent transcript as the worker follows it: how far it has been read, the
		// size it had then, and the usage of what was read so far.
		struct SubagentFile
		{
			std::uint64_t Offset = 0;
			std::uintmax_t Size = 0;
			UsageTally Tally;

			// The call that started this subagent and what its meta file says, once that file
			// has been read. Looked for on every walk until found: the meta file may land after
			// the transcript, and a missing one costs only a failed open.
			std::optional<std::pair<std::string, SubagentInfo>> Link;
		};

		// The worker's own record of every subagent transcript, by path. Only the worker
		// touches it, one read at a time, so it needs no lock and no file IO happens under one.
		struct SubagentFiles
		{
			std::map<std::filesystem::path, SubagentFile> Files;
		};

		// Reads the main transcript from where the last read stopped, a chunk at a time, and
		// publishes each chunk as it goes. False when the transcript is being closed. Worker only.
		static bool ReadMain(const std::filesystem::path& file, Inbox& inbox);

		// Brings every subagent transcript up to date, reading each only from where its last
		// read stopped. Returns the whole state when any of them moved on — appeared, grew,
		// shrank, went, or had its meta file found — and nothing when none did, or when the transcript is being closed.
		// A file that grew only by a line still being written is looked at again next time,
		// but moves nothing. Worker only.
		static std::optional<SubagentState> FollowSubagents(const std::filesystem::path& sessionFile,
														   SubagentFiles& subagents, Inbox& inbox);

		// Shared by both overloads of Take; `timeline` is null for the session-only one.
		bool TakeRecords(Session& session, RequestTimeline* timeline);

		std::filesystem::path m_File;
		std::shared_ptr<Inbox> m_Inbox = std::make_shared<Inbox>();
		std::shared_ptr<SubagentFiles> m_Subagents = std::make_shared<SubagentFiles>();

		// Last, deliberately: joined before the inbox it writes into could go.
		Core::JobQueue m_Jobs{1};
	};
}
