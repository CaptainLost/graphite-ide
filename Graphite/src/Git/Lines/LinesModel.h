#pragma once

#include "Core/Base/Base.h"

#include "Git/DeliveryInbox.h"
#include "Git/GenerationSlot.h"
#include "Git/Lines/LineAnswerChange.h"
#include "Git/Lines/LineReader.h"
#include "Git/Lines/LinesPort.h"
#include "Git/LruResultCache.h"

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
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

		void OnUpdate(); // drains landed results

		void Follow(const Core::Ref<const std::vector<Core::Branch>>& branches); // asks a read when the snapshot differs from the last asked

		void Change(LineAnswerChange change);

		void Clear();

		void SubscribeTo(Core::EventBus& events); // WorkspaceOpenedEvent / WorkspaceClosedEvent -> Clear()

		Core::Ref<const LineRead> GetRead() const;
		const std::optional<Core::Error>& GetError() const;
		bool IsReading() const;

		// Asked on first call, answered from the current read's graph on a worker; empty while pending or when a tip is not in the graph.
		std::optional<Core::LineDistance> DistanceOf(const Core::Oid& from, const Core::Oid& to);
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
		Core::Ref<const std::vector<Core::Branch>> m_WaitingBranches;
		std::deque<LineAnswerChange> m_WaitingChanges;

		LruResultCache<DistanceKey, Core::LineDistance, DistanceKeyHash> m_Distances{kCachedQuestions};
		LruResultCache<HoldingKey, std::vector<bool>, HoldingKeyHash> m_Holdings{kCachedQuestions};

		// WARNING: Bumped by Clear, which the folder events run before the port is pointed at the next folder, and read by
		// every job as it starts: a read or change asked for the folder before must not write the next folder's line store.
		// Only a job stalled between that check and the port opening its repository, while the folder changes, slips past.
		std::atomic<std::uint64_t> m_Folder{0};
	};
}
