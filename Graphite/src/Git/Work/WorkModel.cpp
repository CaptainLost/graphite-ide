#include "Git/Work/WorkModel.h"

#include "Git/Work/WorkBoard.h"

#include <algorithm>
#include <utility>

namespace Graphite {

	WorkModel::WorkModel(WorkPort& port, Core::JobSubmitter submit) : m_Port(port), m_Submit(std::move(submit))
	{
	}

	void WorkModel::OnUpdate(Core::Timestep timestep)
	{
		m_Inbox.Drain();

		// WARNING: A first read still under way is counting every worktree itself; polling beside it would count them
		// twice over on the same workers while the window starts.
		if (!m_OnScreen || m_Polled.empty() || m_Partial != nullptr)
		{
			return;
		}

		m_SincePoll += timestep.GetSeconds();

		if (m_SincePoll >= kPollSeconds)
		{
			m_SincePoll = 0.0f;
			PollCounts();
		}
	}

	void WorkModel::FollowBranches(const std::vector<Core::Branch>& branches)
	{
		WorkFactsRequest request = WorkFactsRequestFor(branches, kPatchBudget);

		if (m_LastAsked.has_value() && *m_LastAsked == request)
		{
			return;
		}

		m_LastAsked = request;
		m_Waiting = std::move(request);
		AskWaiting();
	}

	void WorkModel::Refresh()
	{
		if (!m_LastAsked.has_value())
		{
			return;
		}

		m_Waiting = m_LastAsked;
		AskWaiting();
	}

	void WorkModel::AskWaiting()
	{
		if (!m_Waiting.has_value() || m_Facts.IsInFlight())
		{
			return;
		}

		WorkFactsRequest request = std::move(*m_Waiting);
		m_Waiting.reset();

		const std::uint64_t generation = m_Facts.Begin();

		// WARNING: Only a first read hands out what it has so far. A later one would swap whole facts for partial ones,
		// and every card it had answered would go back to unchecked until the read ends.
		WorkFactsProgress progress;

		if (m_Facts.Get() == nullptr)
		{
			progress = [this, generation](const WorkFacts& soFar) {
				m_Inbox.Post([this, generation, soFar]() { LandPartial(generation, soFar); });
			};
		}

		m_Submit([this, generation, request = std::move(request), progress = std::move(progress)]() {
			Core::Result<WorkFacts> answer = m_Port.ReadWorkFacts(request, progress);

			m_Inbox.Post([this, generation, answer = std::move(answer)]() mutable { LandFacts(generation, std::move(answer)); });
		});
	}

	void WorkModel::LandPartial(std::uint64_t generation, const WorkFacts& soFar)
	{
		// A partial answer of a read already landed, cleared or overtaken is dropped.
		if (!m_Facts.IsInFlight() || generation != m_Facts.GetRequested() || m_Facts.Get() != nullptr)
		{
			return;
		}

		m_Partial = Core::CreateRef<const WorkFacts>(soFar);
		FoldCounts();
	}

	void WorkModel::LandFacts(std::uint64_t generation, Core::Result<WorkFacts> answer)
	{
		const SlotLanding landing = m_Facts.Land(generation, std::move(answer));

		if (landing == SlotLanding::Applied)
		{
			// The read counted every worktree afresh, so counts polled before it are older than its own.
			m_Counts.clear();
			m_Partial.reset();
			m_Folded = m_Facts.Get();
		}

		// WARNING: A first read that failed has landed no facts, so what it handed out on the way goes with it, and the
		// failure is the whole answer.
		if (landing == SlotLanding::Failed && m_Facts.Get() == nullptr)
		{
			m_Partial.reset();
			m_Folded.reset();
		}

		if (landing != SlotLanding::Stale)
		{
			AskWaiting();
		}
	}

	void WorkModel::SetPolled(std::vector<std::filesystem::path> worktrees, bool onScreen)
	{
		if (onScreen && !m_OnScreen)
		{
			m_SincePoll = 0.0f;
		}

		m_Polled = std::move(worktrees);
		m_OnScreen = onScreen;
	}

	bool WorkModel::IsCountInFlight(const std::filesystem::path& worktree) const
	{
		return std::ranges::find(m_CountsInFlight, worktree) != m_CountsInFlight.end();
	}

	void WorkModel::PollCounts()
	{
		for (const std::filesystem::path& worktree : m_Polled)
		{
			if (IsCountInFlight(worktree))
			{
				continue;
			}

			m_CountsInFlight.push_back(worktree);
			const std::uint64_t epoch = m_Epoch;

			m_Submit([this, epoch, worktree]() {
				Core::Result<std::size_t> answer = m_Port.CountUncommitted(worktree);

				m_Inbox.Post([this, epoch, worktree, answer = std::move(answer)]() mutable {
					LandCount(epoch, worktree, std::move(answer));
				});
			});
		}
	}

	void WorkModel::LandCount(std::uint64_t epoch, const std::filesystem::path& worktree, Core::Result<std::size_t> answer)
	{
		if (epoch != m_Epoch)
		{
			return;
		}

		std::erase(m_CountsInFlight, worktree);

		// A count that failed leaves the one held, as a failed facts read leaves the facts.
		if (!answer.HasValue() || m_Folded == nullptr)
		{
			return;
		}

		const auto held = m_Counts.find(worktree);

		if (held != m_Counts.end() && held->second == answer.GetValue())
		{
			return;
		}

		m_Counts[worktree] = answer.GetValue();
		FoldCounts();
	}

	void WorkModel::FoldCounts()
	{
		const Core::Ref<const WorkFacts> base = m_Facts.Get() != nullptr ? m_Facts.Get() : m_Partial;

		if (base == nullptr)
		{
			return;
		}

		WorkFacts folded = *base;

		for (WorktreeFacts& worktree : folded.Worktrees)
		{
			const auto count = m_Counts.find(worktree.Entry.Path);

			if (count != m_Counts.end())
			{
				worktree.Uncommitted = count->second;
			}
		}

		m_Folded = Core::CreateRef<const WorkFacts>(std::move(folded));
	}

	void WorkModel::Clear()
	{
		// WARNING: The slot's generations move on, so a facts read still in flight is dropped when it lands.
		m_Facts.Clear();
		m_Partial.reset();
		m_Folded.reset();
		m_LastAsked.reset();
		m_Waiting.reset();
		m_Counts.clear();
		m_CountsInFlight.clear();
		++m_Epoch;
	}
}
