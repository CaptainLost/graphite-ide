#pragma once

#include "Core/Base/Base.h"

#include "Git/DeliveryInbox.h"
#include "Git/GenerationSlot.h"
#include "Git/Lines/LineAnswerChange.h"
#include "Git/Lines/LineReader.h"
#include "Git/Lines/LinesPort.h"
#include "Git/Lines/ReadRetry.h"
#include "Git/LruResultCache.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Base/Timestep.h"
#include "Core/Events/EventBus.h"
#include "Core/Git/Lines/LineQuestions.h"
#include "Core/Git/Oid.h"
#include "Core/Git/Ref.h"
#include "Core/Jobs/JobSubmitter.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

namespace Graphite {

	// The lines of the open folder, read on a worker for each branch snapshot, and the user's answers saved and applied
	// one at a time. UI thread only, but for the jobs it submits.
	// WARNING: Every job captures `this`; the owner declares this before the queue behind the submitter.
	class LinesModel : Core::NonCopyable
	{
	public:
		LinesModel(LinesPort& port, Core::JobSubmitter submit, std::function<std::int64_t()> now);

		void OnUpdate(Core::Timestep timestep); // drains landed results and asks a failed read again once its back-off ends

		// Asks a read at once when the snapshot differs from the last asked. When the last read or change failed and the
		// branches were listed again since, as a refresh lists them even when nothing changed, asks it again once the
		// back-off after the failures in a row ends. `listing` counts the listings.
		void Follow(const Core::Ref<const std::vector<Core::Branch>>& branches, std::uint64_t listing = 0);

		void Change(LineAnswerChange change);

		// A change that could not be saved, once, to be said where the user made it; the read before it stays.
		std::optional<Core::Error> TakeChangeFailure();

		void Clear();

		void SubscribeTo(Core::EventBus& events); // WorkspaceOpenedEvent / WorkspaceClosedEvent -> Clear()

		Core::Ref<const LineRead> GetRead() const;
		const std::optional<Core::Error>& GetError() const;
		bool IsReading() const;

		// Asked on first call, answered from the current read's graph on a worker; empty while pending or when a tip is not in the graph.
		std::optional<Core::LineDistance> DistanceOf(const Core::Oid& from, const Core::Oid& to);

		// The current read's graph holds the commit, so a distance from or to it is answered in time.
		bool HoldsCommit(const Core::Oid& commit) const;
		std::optional<std::vector<bool>> BasesHolding(const Core::Oid& commit); // one flag per GetRead()->Model.Bases entry

		static constexpr std::size_t kCachedQuestions = 256;

	private:
		struct DistanceKey
		{
			Core::Oid From;
			Core::Oid To;

			friend bool operator==(const DistanceKey&, const DistanceKey&) = default;
		};

		struct DistanceKeyHash
		{
			std::size_t operator()(const DistanceKey& key) const noexcept;
		};

		struct HoldingKey
		{
			Core::Oid Commit;
			std::vector<Core::Oid> BaseTips; // in the order of the bases the flags answer

			friend bool operator==(const HoldingKey&, const HoldingKey&) = default;
		};

		struct HoldingKeyHash
		{
			std::size_t operator()(const HoldingKey& key) const noexcept;
		};

		void Deliver(DeliveryInbox::Delivery delivery);

		void AskRead(const Core::Ref<const std::vector<Core::Branch>>& branches);
		void RetryWhenDue();

		// Starts what waits, when nothing runs: the oldest change first, then the newest snapshot.
		void RunNextWaiting();
		void StartRead(const Core::Ref<const LineRead>& last);
		void StartChange(const Core::Ref<const LineRead>& last);
		void Land(std::uint64_t generation, Core::Result<LineRead> result);

		// Read on worker threads: a job of a folder cleared since it was asked does nothing.
		bool IsSameFolder(std::uint64_t folder) const;

		LinesPort& m_Port;
		Core::JobSubmitter m_Submit;
		std::function<std::int64_t()> m_Now;

		// WARNING: The inbox's mutex covers the inbox and nothing else; every other member but m_Folder is touched
		// on the UI thread only.
		DeliveryInbox m_Inbox;

		// One generation per read or change; at most one is in flight.
		GenerationSlot<LineRead> m_Read;

		Core::Ref<const std::vector<Core::Branch>> m_LastAsked;
		std::uint64_t m_LastListing = 0;
		Core::Ref<const std::vector<Core::Branch>> m_WaitingBranches;

		// Reset by a new snapshot, a line change, a read or change that succeeds, and a folder switch.
		ReadRetry m_Retry;
		bool m_RetryWanted = false; // listed again after a failure, so the read is asked again once the back-off ends
		double m_Clock = 0.0;       // seconds of OnUpdate time

		// The generation of the last change run, so its failure is told from a read's.
		std::uint64_t m_ChangeGeneration = 0;
		std::optional<Core::Error> m_ChangeFailure;
		std::deque<LineAnswerChange> m_WaitingChanges;

		LruResultCache<DistanceKey, Core::LineDistance, DistanceKeyHash> m_Distances{kCachedQuestions};
		LruResultCache<HoldingKey, std::vector<bool>, HoldingKeyHash> m_Holdings{kCachedQuestions};

		// WARNING: Bumped by Clear, which the folder events run before the port is pointed at the next folder, and read by
		// every job as it starts: a read or change asked for the folder before must not write the next folder's line store.
		// A job stalled between that check and the port opening its repository, while the folder changes, is stopped by the
		// port instead: a read by the repository it was asked for, a change by the store of the read it was asked on.
		std::atomic<std::uint64_t> m_Folder{0};
	};
}
