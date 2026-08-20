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
		m_DiffApplied = m_DiffRequested;

		m_Status.reset();
		m_Branches.reset();
		m_Log.reset();
		m_Diff.reset();
		m_CommitDiffs.clear();

		m_StatusError.reset();
		m_BranchesError.reset();
		m_LogError.reset();
		m_DiffError.reset();

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

	void GitModel::RequestDiff(Core::DiffRequest request)
	{
		const std::uint64_t generation = ++m_DiffRequested;

		m_Submit([this, generation, request = std::move(request)]() {
			auto result = m_Port.QueryDiff(request);

			Deliver([this, generation, result = std::move(result)]() mutable {
				if (generation <= m_DiffApplied)
				{
					return;
				}

				m_DiffApplied = generation;

				if (!result.HasValue())
				{
					m_DiffError = result.GetError();
					return;
				}

				m_DiffError.reset();
				m_Diff = std::make_shared<const Core::Diff>(std::move(result).GetValue());
			});
		});
	}

	void GitModel::Touch(CachedDiff& entry) const
	{
		entry.LastTouched = ++m_DiffClock;
	}

	void GitModel::EvictIfFull()
	{
		if (m_CommitDiffs.size() < kCachedDiffs)
		{
			return;
		}

		auto oldest = m_CommitDiffs.end();

		for (auto entry = m_CommitDiffs.begin(); entry != m_CommitDiffs.end(); ++entry)
		{
			// An entry still in flight is never evicted: its delivery would then arrive
			// for a key nobody holds and be dropped, and the reader would sit looking at
			// a row that never fills in.
			if (entry->second.Requested != entry->second.Applied)
			{
				continue;
			}

			if (oldest == m_CommitDiffs.end() || entry->second.LastTouched < oldest->second.LastTouched)
			{
				oldest = entry;
			}
		}

		if (oldest != m_CommitDiffs.end())
		{
			m_CommitDiffs.erase(oldest);
		}
	}

	void GitModel::RequestDiffFor(const Core::Oid& commit)
	{
		if (const auto existing = m_CommitDiffs.find(commit); existing != m_CommitDiffs.end())
		{
			const bool inFlight = existing->second.Requested != existing->second.Applied;

			if (inFlight || existing->second.Diff != nullptr)
			{
				Touch(existing->second);
				return;
			}
		}

		EvictIfFull();

		CachedDiff& entry = m_CommitDiffs[commit];
		Touch(entry);

		const std::uint64_t generation = ++entry.Requested;

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
					// that failed has nothing older to fall back to.
					m_DiffError = result.GetError();
					return;
				}

				m_DiffError.reset();
				entry->second.Diff =
					std::make_shared<const Core::Diff>(std::move(result).GetValue());
			});
		});
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
