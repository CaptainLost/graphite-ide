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

		m_Status.reset();
		m_Branches.reset();
		m_Log.reset();

		m_StatusError.reset();
		m_BranchesError.reset();
		m_LogError.reset();

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
				m_Log = std::make_shared<const std::vector<Core::CommitSummary>>(
					std::move(result).GetValue());
			});
		});
	}
}
