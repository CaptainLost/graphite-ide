#include "Git/GitModel.h"

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

	void GitModel::OnUpdate(Core::Timestep)
	{
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
					m_LastError = result.GetError();
					return;
				}

				m_LastError.reset();
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
					m_LastError = result.GetError();
					return;
				}

				m_LastError.reset();
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
					m_LastError = result.GetError();
					return;
				}

				m_LastError.reset();
				m_Log = std::make_shared<const std::vector<Core::CommitSummary>>(
					std::move(result).GetValue());
			});
		});
	}
}
