#pragma once

#include "Sessions/SessionSummary.h"

#include "Core/Jobs/CancellationToken.h"
#include "Core/Jobs/JobQueue.h"

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
	// is the thread.
	void ScanProjects(const std::filesystem::path& projects, const std::vector<SessionSummary>& cached,
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

		bool IsScanning() const;

		// UI thread only.
		void Take(IndexDelivery& out);

	private:
		struct Inbox
		{
			std::mutex Mutex;
			IndexDelivery Ready;
			std::size_t Pending = 0;
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
		std::shared_ptr<Inbox> m_Inbox = std::make_shared<Inbox>();
		std::shared_ptr<Known> m_Known = std::make_shared<Known>();
		Core::CancellationToken m_Token;

		// Last, deliberately: destroyed first, so the worker is joined before what it touches.
		Core::JobQueue m_Jobs{1};
	};
}
