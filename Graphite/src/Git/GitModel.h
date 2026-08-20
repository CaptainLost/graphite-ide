#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Git/GitPort.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace Graphite {

	// How a query reaches a worker. Injected rather than taken as a Core::JobQueue so the
	// tests can run a job where they submitted it and stay free of threads.
	using JobSubmitter = std::function<void(std::function<void()>)>;

	// Holds what the git views draw and decides when it is stale.
	//
	// Two rules run through the whole class. Results are immutable and handed out by
	// shared pointer, so a view that took one keeps drawing a consistent list even as the
	// next refresh lands. And every request carries a generation, so a slow query that
	// finishes after a newer one is dropped instead of putting old data back on screen.
	// Every job submitted below captures `this`, so the model requires that the queue
	// behind its submitter is destroyed - and its workers joined - before the model
	// itself is. An owner holding both the model and the queue must declare the model
	// BEFORE the queue, so reverse member destruction tears the queue down first, before
	// this object goes away. Getting the order backwards is a use-after-free that will
	// not show up under the inline submitter the tests use.
	class GitModel : Core::NonCopyable
	{
	public:
		GitModel(GitPort& port, JobSubmitter submit);

		// Applies whatever the workers finished since the last frame. Called once per
		// frame from the layer; results only become visible here, never mid-frame.
		void OnUpdate(Core::Timestep ts);

		void RequestStatus();
		void RequestBranches();
		void RequestLog(Core::LogRequest request);

		// Null until the first result for that query has been applied.
		std::shared_ptr<const Core::WorkingTreeStatus> GetStatus() const { return m_Status; }
		std::shared_ptr<const std::vector<Core::Branch>> GetBranches() const { return m_Branches; }
		std::shared_ptr<const std::vector<Core::CommitSummary>> GetLog() const { return m_Log; }

		// The failure a panel should show, if any. Each query kind keeps its own error, set
		// by its own failure and cleared only by its own success, so a branches refresh
		// that succeeds can never hide a status refresh that is still failing. When more
		// than one is set this returns the first in the order status, branches, log -
		// status is checked most often and is the one a user most needs to see fail.
		const std::optional<Core::Error>& GetLastError() const
		{
			if (m_StatusError.has_value())
			{
				return m_StatusError;
			}

			if (m_BranchesError.has_value())
			{
				return m_BranchesError;
			}

			return m_LogError;
		}

	private:
		using Delivery = std::function<void()>;

		void Deliver(Delivery delivery);

		GitPort& m_Port;
		JobSubmitter m_Submit;

		// Written by workers, drained by OnUpdate. The mutex covers this and nothing else:
		// every snapshot below is only ever touched on the UI thread.
		std::mutex m_InboxMutex;
		std::vector<Delivery> m_Inbox;

		std::uint64_t m_StatusRequested = 0;
		std::uint64_t m_StatusApplied = 0;
		std::uint64_t m_BranchesRequested = 0;
		std::uint64_t m_BranchesApplied = 0;
		std::uint64_t m_LogRequested = 0;
		std::uint64_t m_LogApplied = 0;

		std::shared_ptr<const Core::WorkingTreeStatus> m_Status;
		std::shared_ptr<const std::vector<Core::Branch>> m_Branches;
		std::shared_ptr<const std::vector<Core::CommitSummary>> m_Log;

		std::optional<Core::Error> m_StatusError;
		std::optional<Core::Error> m_BranchesError;
		std::optional<Core::Error> m_LogError;
	};
}
