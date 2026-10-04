#pragma once

#include "Core/Base/Base.h"

#include "Sessions/SessionSummary.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace Graphite {

	// The session transcripts directly inside one project directory.
	std::vector<std::filesystem::path> SessionFilesUnder(const std::filesystem::path& projectDirectory);

	// Every session under `projects`, taken from `cached` where the fingerprint still matches
	// and summarised again where it does not. Synchronous, and tested that way — SessionIndex
	// is the thread. A file that cannot be read is left out, to be tried again when it next
	// changes. Returns the cached sessions whose file the scan did not find — deleted while
	// nothing was watching — or nothing when the scan was cancelled and so did not look
	// everywhere.
	std::vector<std::filesystem::path> ScanProjects(const std::filesystem::path& projects, const std::vector<SessionSummary>& cached,
													const Core::CancellationToken& token,
													const std::function<void(SessionSummary)>& onSummary);

	// A directory under `<project>/<session-id>/` belongs to that session: its subagents and
	// stored tool results live there. Anything shallower is not one session's.
	std::optional<std::filesystem::path> SessionFileFor(const std::filesystem::path& projects,
														const std::filesystem::path& changedDirectory);

	struct IndexDelivery
	{
		std::vector<SessionSummary> Updated;
		std::vector<std::filesystem::path> Removed;
	};

	// How far the first full scan has got. Watcher refreshes are not part of it: a session
	// that is being written keeps the index busy indefinitely, and the scan is long over.
	struct ScanProgress
	{
		bool Done = false;
		std::size_t Scanned = 0;
		std::size_t Total = 0;
	};

	// Summaries of every session, kept current on a worker.
	class SessionIndex
	{
	public:
		~SessionIndex();

		SessionIndex() = default;
		SessionIndex(const SessionIndex&) = delete;
		SessionIndex& operator=(const SessionIndex&) = delete;

		void Start(std::filesystem::path projects, std::vector<SessionSummary> cached);

		// Re-examines what the watcher reported. `projects` itself, or a new project directory,
		// means everything; a project directory means its sessions; a directory inside a
		// session's folder means that session.
		void Refresh(std::vector<std::filesystem::path> changedDirectories);

		// Whether any work is queued or running, refreshes included. For a caller that needs
		// the index quiet, as a test does; a list that shows progress asks InitialScan.
		bool IsScanning() const;

		// Whether the scan Start began has finished, and how many session files it has looked
		// at out of how many it found. Read on any thread.
		ScanProgress InitialScan() const;

		// UI thread only.
		void Take(IndexDelivery& out);

	private:
		struct Inbox
		{
			std::mutex Mutex;
			IndexDelivery Ready;
			std::size_t Pending = 0;

			// Written by the start job, read by the UI thread, without the lock: the count
			// moves once per session file, not per byte.
			std::atomic<bool> InitialScanDone = false;
			std::atomic<std::size_t> Scanned = 0;
			std::atomic<std::size_t> Total = 0;
		};

		// Touched only by the worker: the fingerprint each session had when it was last
		// delivered, which is how a refresh knows what changed and what disappeared. One
		// worker, so no lock.
		struct Known
		{
			std::map<std::filesystem::path, Fingerprint> Prints;
		};

		void Submit(std::function<void(Inbox&, Known&)> work);

		std::filesystem::path m_Projects;
		Core::Ref<Inbox> m_Inbox = Core::CreateRef<Inbox>();
		Core::Ref<Known> m_Known = Core::CreateRef<Known>();
		Core::CancellationToken m_Token;

		// Last, deliberately: destroyed first, so the worker is joined before what it touches.
		Core::JobQueue m_Jobs{1};
	};
}
