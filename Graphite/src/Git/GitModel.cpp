#include "Git/GitModel.h"

#include "Workspace/Workspace.h"

#include <utility>

namespace Graphite {

	GitModel::GitModel(GitPort& port, JobSubmitter submit)
		: m_Port(port), m_Submit(std::move(submit))
	{
	}

	void GitModel::Deliver(Delivery delivery)
	{
		const std::lock_guard<std::mutex> lock{m_InboxMutex};
		m_Inbox.push_back(std::move(delivery));
	}

	void GitModel::SetRoot(std::filesystem::path root)
	{
		m_Root = std::move(root);
	}

	void GitModel::Clear()
	{
		// The generations move forward rather than resetting, so a query still in flight
		// for the folder that was just closed is dropped when it lands.
		m_StatusApplied = m_StatusRequested;
		m_BranchesApplied = m_BranchesRequested;
		m_LogApplied = m_LogRequested;
		m_WorkingTreeDiffApplied = m_WorkingTreeDiffRequested;

		m_Status.reset();
		m_Branches.reset();
		m_Log.reset();
		m_WorkingTreeDiff.reset();
		m_CommitDiffs.clear();

		m_StatusError.reset();
		m_BranchesError.reset();
		m_LogError.reset();
		m_WorkingTreeDiffError.reset();
		m_CommitDiffError.reset();

		m_Dirty = false;
		m_DirtyFor = 0.0f;
		m_DirtySince = 0.0f;
	}

	void GitModel::MarkDirty()
	{
		if (m_Root.empty())
		{
			return;
		}

		// The burst's start is only reset when it was not already dirty: a mark that arrives
		// mid-burst extends the quiet timer but must not push the cap out further, or a
		// sustained stream of events would never hit it.
		if (!m_Dirty)
		{
			m_DirtySince = 0.0f;
		}

		m_Dirty = true;
		m_DirtyFor = 0.0f;
	}

	void GitModel::SubscribeTo(Core::EventBus& events)
	{
		events.Subscribe<WorkspaceOpenedEvent>([this](const WorkspaceOpenedEvent& event) {
			// Opening a folder does not always follow a WorkspaceClosedEvent - dropping a
			// second folder onto the window replaces the workspace directly. Without Clear()
			// here, GetStatus()/GetBranches() would keep answering with the previous folder's
			// snapshot for the debounce window plus one query: a lie about the folder now
			// open. Clear() before SetRoot and MarkDirty, since Clear() sets m_Dirty = false
			// and would otherwise undo the mark.
			Clear();
			SetRoot(event.Root);
			MarkDirty();
		});

		events.Subscribe<WorkspaceClosedEvent>([this](const WorkspaceClosedEvent&) {
			m_Root.clear();
			Clear();
		});

		events.Subscribe<WorkspaceFilesChangedEvent>(
			[this](const WorkspaceFilesChangedEvent&) { MarkDirty(); });
	}

	void GitModel::OnUpdate(Core::Timestep ts)
	{
		if (m_Dirty)
		{
			m_DirtyFor += ts.GetSeconds();
			m_DirtySince += ts.GetSeconds();

			if (m_DirtyFor >= kDebounceSeconds || m_DirtySince >= kMaxDebounceSeconds)
			{
				m_Dirty = false;
				m_DirtyFor = 0.0f;
				m_DirtySince = 0.0f;

				RequestStatus();
				RequestBranches();
			}
		}

		std::vector<Delivery> ready;

		{
			const std::lock_guard<std::mutex> lock{m_InboxMutex};
			ready.swap(m_Inbox);
		}

		for (const Delivery& delivery : ready)
		{
			delivery();
		}
	}

	void GitModel::RequestStatus()
	{
		const std::uint64_t generation = ++m_StatusRequested;

		m_Submit([this, generation]() {
			auto result = m_Port.QueryStatus();

			Deliver([this, generation, result = std::move(result)]() mutable {
				// A query that started earlier and finished later would otherwise put a
				// stale list back on screen.
				if (generation <= m_StatusApplied)
				{
					return;
				}

				m_StatusApplied = generation;

				if (!result.HasValue())
				{
					// The last good snapshot stays. A panel that blanks itself because one
					// refresh failed is worse than one showing data a second out of date.
					m_StatusError = result.GetError();
					return;
				}

				m_StatusError.reset();
				m_Status = std::make_shared<const Core::WorkingTreeStatus>(
					std::move(result).GetValue());
			});
		});
	}

	void GitModel::RequestBranches()
	{
		const std::uint64_t generation = ++m_BranchesRequested;

		m_Submit([this, generation]() {
			auto result = m_Port.QueryBranches();

			Deliver([this, generation, result = std::move(result)]() mutable {
				if (generation <= m_BranchesApplied)
				{
					return;
				}

				m_BranchesApplied = generation;

				if (!result.HasValue())
				{
					m_BranchesError = result.GetError();
					return;
				}

				m_BranchesError.reset();
				m_Branches = std::make_shared<const std::vector<Core::Branch>>(
					std::move(result).GetValue());
			});
		});
	}

	void GitModel::RequestLog(Core::LogRequest request)
	{
		// Mirrors MarkDirty: nothing on screen has any business asking about a folder that
		// is not open. WorkspaceClosedEvent's handler resets GitView's log-requested latch
		// without knowing whether a new folder will open next, so without this a closed
		// workspace would still fire one query for the folder it no longer has.
		if (m_Root.empty())
		{
			return;
		}

		const std::uint64_t generation = ++m_LogRequested;

		m_Submit([this, generation, request = std::move(request)]() {
			auto result = m_Port.QueryLog(request);

			Deliver([this, generation, result = std::move(result)]() mutable {
				if (generation <= m_LogApplied)
				{
					return;
				}

				m_LogApplied = generation;

				if (!result.HasValue())
				{
					m_LogError = result.GetError();
					return;
				}

				m_LogError.reset();
				m_Log = std::make_shared<const Core::LogPage>(std::move(result).GetValue());
			});
		});
	}

	void GitModel::RequestWorkingTreeDiff(Core::DiffRequest request)
	{
		const std::uint64_t generation = ++m_WorkingTreeDiffRequested;

		m_Submit([this, generation, request = std::move(request)]() {
			auto result = m_Port.QueryDiff(request);

			Deliver([this, generation, result = std::move(result)]() mutable {
				if (generation <= m_WorkingTreeDiffApplied)
				{
					return;
				}

				m_WorkingTreeDiffApplied = generation;

				if (!result.HasValue())
				{
					m_WorkingTreeDiffError = result.GetError();
					return;
				}

				m_WorkingTreeDiffError.reset();
				m_WorkingTreeDiff =
					std::make_shared<const Core::Diff>(std::move(result).GetValue());
			});
		});
	}

	void GitModel::Touch(CachedDiff& entry) const
	{
		entry.LastTouched = ++m_DiffClock;
	}

	void GitModel::EvictIfFull()
	{
		// A loop, not one shot: a single eviction only ever drops one entry, so once the
		// map is above the bound - which a run of requests that are all still in flight can
		// leave it at, since none of them were evictable at the time - one-in-one-out on
		// every later request would hold it there forever instead of bringing it back down.
		while (m_CommitDiffs.size() >= kCachedDiffs)
		{
			auto oldest = m_CommitDiffs.end();

			for (auto entry = m_CommitDiffs.begin(); entry != m_CommitDiffs.end(); ++entry)
			{
				// An entry still in flight is never evicted: its delivery would then arrive
				// for a key nobody holds and be dropped, and the reader would sit looking
				// at a row that never fills in.
				if (entry->second.Requested != entry->second.Applied)
				{
					continue;
				}

				if (oldest == m_CommitDiffs.end() || entry->second.LastTouched < oldest->second.LastTouched)
				{
					oldest = entry;
				}
			}

			// Nothing evictable this pass - every remaining entry is in flight. Stop rather
			// than spin: the map stays over the bound until one of them lands.
			if (oldest == m_CommitDiffs.end())
			{
				break;
			}

			m_CommitDiffs.erase(oldest);
		}
	}

	void GitModel::RequestDiffFor(const Core::Oid& commit)
	{
		// Mirrors MarkDirty and RequestLog: nothing on screen has any business asking about
		// a commit's diff for a folder that is not open.
		if (m_Root.empty())
		{
			return;
		}

		const auto existing = m_CommitDiffs.find(commit);

		if (existing != m_CommitDiffs.end())
		{
			const bool inFlight = existing->second.Requested != existing->second.Applied;

			// A failed entry is suppressed the same way a held or in-flight one is: the list
			// asks about every expanded commit every frame, and without this a permanent
			// failure would resubmit a worker job that many times a second.
			// ForgetFailedDiffFor is the only way back in.
			if (inFlight || existing->second.Diff != nullptr || existing->second.Failed)
			{
				Touch(existing->second);
				return;
			}
		}
		else
		{
			// Only when the key is absent: this is the only branch that grows the map. A
			// held, in-flight or failed entry above returns before reaching here, so
			// evicting there - where the key is already present and nothing will grow -
			// would drop an unrelated entry for no reason.
			EvictIfFull();
		}

		CachedDiff& entry = m_CommitDiffs[commit];
		Touch(entry);

		// From the model-wide counter, not a counter local to the entry - see the comment
		// on CachedDiff::Requested.
		const std::uint64_t generation = ++m_CommitDiffGeneration;
		entry.Requested = generation;

		Core::DiffRequest request;
		request.Target = Core::DiffTarget::CommitToParent;
		request.From = commit;

		m_Submit([this, commit, generation, request]() {
			auto result = m_Port.QueryDiff(request);

			Deliver([this, commit, generation, result = std::move(result)]() mutable {
				const auto entry = m_CommitDiffs.find(commit);

				// Evicted, or the workspace closed, while the query was running. Dropping
				// it is right: nothing on screen is waiting for it.
				if (entry == m_CommitDiffs.end() || generation <= entry->second.Applied)
				{
					return;
				}

				entry->second.Applied = generation;

				if (!result.HasValue())
				{
					// No last-good snapshot to keep, unlike the volatile diff: a commit
					// that failed has nothing older to fall back to. Marked rather than left
					// to imply itself from a null Diff, so RequestDiffFor can tell "still
					// waiting" from "already tried and lost" without a second lookup.
					m_CommitDiffError = result.GetError();
					entry->second.Failed = true;
					return;
				}

				m_CommitDiffError.reset();
				entry->second.Diff =
					std::make_shared<const Core::Diff>(std::move(result).GetValue());
			});
		});
	}

	void GitModel::ForgetFailedDiffFor(const Core::Oid& commit)
	{
		const auto entry = m_CommitDiffs.find(commit);

		// A successful entry stays: this is what makes the cache hold recently-seen commits
		// rather than only the currently-expanded ones. Only a failed entry is dropped, so
		// collapsing and re-expanding the row is the reader's way to retry it.
		if (entry == m_CommitDiffs.end() || !entry->second.Failed)
		{
			return;
		}

		m_CommitDiffs.erase(entry);

		// The error line must not outlive the row it described.
		m_CommitDiffError.reset();
	}

	std::shared_ptr<const Core::Diff> GitModel::GetDiffFor(const Core::Oid& commit) const
	{
		const auto entry = m_CommitDiffs.find(commit);

		if (entry == m_CommitDiffs.end())
		{
			return nullptr;
		}

		Touch(entry->second);
		return entry->second.Diff;
	}
}
