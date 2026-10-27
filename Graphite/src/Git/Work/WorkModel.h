#pragma once

#include "Core/Base/Base.h"

#include "Git/DeliveryInbox.h"
#include "Git/GenerationSlot.h"
#include "Git/Work/WorkPort.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Timestep.h"
#include "Core/Git/Ref.h"
#include "Core/Jobs/JobSubmitter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace Graphite {

	// WARNING: Every job captures `this`, so the queue behind the submitter must be destroyed, and its
	// workers joined, before this object is. An owner holding both declares this BEFORE the queue.
	class WorkModel : Core::NonCopyable
	{
	public:
		WorkModel(WorkPort& port, Core::JobSubmitter submit);

		void OnUpdate(Core::Timestep timestep);

		// Asks the facts again only when the request built from these branches differs from the last one asked.
		void FollowBranches(const std::vector<Core::Branch>& branches);

		// While onScreen, each of these worktrees' uncommitted count is read again every kPollSeconds; off screen never.
		void SetPolled(std::vector<std::filesystem::path> worktrees, bool onScreen);

		void Refresh();
		void Clear();

		// The last answer, with polled counts folded in.
		Core::Ref<const WorkFacts> GetFacts() const { return m_Folded; }

		const std::optional<Core::Error>& GetError() const { return m_Facts.GetError(); }

		bool IsReading() const { return m_Facts.IsInFlight() || m_Waiting.has_value(); }

		static constexpr std::size_t kPatchBudget = 2000;
		static constexpr float kPollSeconds = 3.0f;

	private:
		// WARNING: At most one facts read runs at a time; whatever is asked meanwhile waits here, and only the newest.
		void AskWaiting();
		void LandFacts(std::uint64_t generation, Core::Result<WorkFacts> answer);
		void PollCounts();
		void LandCount(std::uint64_t epoch, const std::filesystem::path& worktree, Core::Result<std::size_t> answer);
		void FoldCounts();

		bool IsCountInFlight(const std::filesystem::path& worktree) const;

		WorkPort& m_Port;
		Core::JobSubmitter m_Submit;
		DeliveryInbox m_Inbox;

		GenerationSlot<WorkFacts> m_Facts;
		Core::Ref<const WorkFacts> m_Folded;
		std::optional<WorkFactsRequest> m_LastAsked;
		std::optional<WorkFactsRequest> m_Waiting;

		// Bumped by Clear, so a count read before it is dropped when it lands.
		std::uint64_t m_Epoch = 0;
		std::vector<std::filesystem::path> m_Polled;
		std::vector<std::filesystem::path> m_CountsInFlight;
		std::map<std::filesystem::path, std::size_t> m_Counts;
		bool m_OnScreen = false;
		float m_SincePoll = 0.0f;
	};
}
