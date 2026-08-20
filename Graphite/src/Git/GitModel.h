#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Git/GitPort.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
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

		// A diff is requested by an explicit selection rather than by the debounce: it
		// answers "what changed in the thing I just clicked", and nothing on disk can
		// change which thing that is.
		void RequestDiff(Core::DiffRequest request);

		// The diff of one commit against its first parent. Requested by expanding a row,
		// and held afterwards: a commit's diff cannot change, so re-asking would be pure
		// waste — and the list asks about every expanded commit on every frame it draws.
		//
		// Asking for a commit already held, or already in flight, does nothing.
		void RequestDiffFor(const Core::Oid& commit);

		// Null while the answer is in flight, after a failure, and for a commit that was
		// evicted. In every case the caller's response is the same: draw nothing under
		// that row yet.
		std::shared_ptr<const Core::Diff> GetDiffFor(const Core::Oid& commit) const;

		// Thirty-two is well past any number of rows a reader expands at once, and bounds
		// the memory a long scroll can accumulate. Evicting the least recently asked for
		// costs a re-query if the reader scrolls back, which is a worker-thread walk they
		// will not notice, against holding every diff they ever passed.
		static constexpr std::size_t kCachedDiffs = 32;

		// Null until the first result for that query has been applied.
		std::shared_ptr<const Core::WorkingTreeStatus> GetStatus() const { return m_Status; }
		std::shared_ptr<const std::vector<Core::Branch>> GetBranches() const { return m_Branches; }
		std::shared_ptr<const Core::LogPage> GetLog() const { return m_Log; }
		std::shared_ptr<const Core::Diff> GetDiff() const { return m_Diff; }

		// The failure a panel should show, if any. Each query kind keeps its own error, set
		// by its own failure and cleared only by its own success, so a branches refresh
		// that succeeds can never hide a status refresh that is still failing. When more
		// than one is set this returns the first in the order status, branches, log, diff -
		// status is checked most often and is the one a user most needs to see fail, and a
		// diff failure is the least likely of the four to be what a user needs told first.
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

			if (m_LogError.has_value())
			{
				return m_LogError;
			}

			return m_DiffError;
		}

		// Without a root the model answers nothing: a file-change event that arrives
		// before a folder is open has no repository to ask about.
		void SetRoot(std::filesystem::path root);
		void Clear();

		void SubscribeTo(Core::EventBus& events);

		// How long a burst of file-change events is allowed to keep coalescing. A save
		// touches several files, and refreshing per file would run the same query five
		// times for one action.
		static constexpr float kDebounceSeconds = 0.15f;

		// A ceiling on how long coalescing can postpone the refresh, measured from the first
		// event of the burst rather than the most recent one. Without this, a sustained
		// stream of change events - a build writing into an output directory, which
		// Workspace::Update republishes per settled batch - would keep resetting the quiet
		// timer and starve the refresh for as long as the stream continues, rather than
		// coalescing to one refresh per quiet period.
		static constexpr float kMaxDebounceSeconds = 1.0f;

	private:
		using Delivery = std::function<void()>;

		void Deliver(Delivery delivery);

		void MarkDirty();

		struct CachedDiff
		{
			// Zero until an answer lands. Non-zero with a null Diff means "in flight".
			std::uint64_t Requested = 0;
			std::uint64_t Applied = 0;

			std::shared_ptr<const Core::Diff> Diff;

			// Counts up on every request and every read, so eviction can pick the entry
			// nobody has looked at for longest. A counter rather than a clock: the model
			// must stay testable without one.
			std::uint64_t LastTouched = 0;
		};

		void Touch(CachedDiff& entry) const;
		void EvictIfFull();

		std::filesystem::path m_Root;

		bool m_Dirty = false;
		float m_DirtyFor = 0.0f;

		// Time since the burst's first mark, not its most recent one - see kMaxDebounceSeconds.
		float m_DirtySince = 0.0f;

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
		std::uint64_t m_DiffRequested = 0;
		std::uint64_t m_DiffApplied = 0;

		std::shared_ptr<const Core::WorkingTreeStatus> m_Status;
		std::shared_ptr<const std::vector<Core::Branch>> m_Branches;
		std::shared_ptr<const Core::LogPage> m_Log;
		std::shared_ptr<const Core::Diff> m_Diff;

		// Mutable because GetDiffFor is const and still counts as a use — an entry the
		// reader is looking at every frame must not be the one evicted.
		mutable std::unordered_map<Core::Oid, CachedDiff> m_CommitDiffs;
		mutable std::uint64_t m_DiffClock = 0;

		std::optional<Core::Error> m_StatusError;
		std::optional<Core::Error> m_BranchesError;
		std::optional<Core::Error> m_LogError;
		std::optional<Core::Error> m_DiffError;
	};
}
