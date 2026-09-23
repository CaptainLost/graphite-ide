#pragma once

#include "Sessions/Session.h"
#include "Sessions/TranscriptRecord.h"

#include "Core/Jobs/JobQueue.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
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

		std::size_t GetSkipped() const;
		const std::filesystem::path& GetFile() const { return m_File; }

	private:
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
		};

		std::filesystem::path m_File;
		std::shared_ptr<Inbox> m_Inbox = std::make_shared<Inbox>();

		// Last, deliberately: joined before the inbox it writes into could go.
		Core::JobQueue m_Jobs{1};
	};
}
