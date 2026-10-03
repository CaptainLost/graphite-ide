#pragma once

#include "Core/Base/Result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace Graphite {

	// What became of an answer handed to GenerationSlot::Land.
	enum class SlotLanding
	{
		// Older than an answer already applied, or sent before a Clear: dropped untouched.
		Stale,

		// Applied as an error. The last good snapshot stays.
		Failed,

		// Applied as the new snapshot.
		Applied
	};

	// One query whose answer is replaced on every refresh - the status, the branch list, the
	// log - with the generation bookkeeping that keeps a slow answer from putting old data
	// back on screen.
	//
	// Every request takes the next generation, and an answer lands only if it is newer than
	// the last one applied, so a query that started earlier and finished later is dropped.
	// The snapshot is immutable and handed out by shared pointer, so a reader holding one
	// keeps a consistent value as the next answer lands. The error is the slot's own: set by
	// its failure and cleared only by its success, so another query's success never hides it.
	//
	// UI thread only.
	template<typename T>
	class GenerationSlot
	{
	public:
		// The generation to send the next query under.
		std::uint64_t Begin() { return ++m_Requested; }

		// A failure keeps the last good snapshot: a panel that blanks itself because one
		// refresh failed is worse than one showing data a second out of date.
		SlotLanding Land(std::uint64_t generation, Core::Result<T> result)
		{
			if (generation <= m_Applied)
			{
				return SlotLanding::Stale;
			}

			m_Applied = generation;

			if (!result.HasValue())
			{
				m_Error = result.GetError();
				return SlotLanding::Failed;
			}

			m_Error.reset();
			m_Value = std::make_shared<const T>(std::move(result).GetValue());
			return SlotLanding::Applied;
		}

		// Null until the first successful answer has landed, and after a Clear.
		std::shared_ptr<const T> Get() const { return m_Value; }

		const std::optional<Core::Error>& GetError() const { return m_Error; }

		// True between a Begin and the landing of the answer to the latest one.
		bool IsInFlight() const { return m_Requested != m_Applied; }

		std::uint64_t GetRequested() const { return m_Requested; }
		std::uint64_t GetApplied() const { return m_Applied; }

		// Forgets the snapshot and the error. The generations move forward rather than
		// resetting, so an answer still in flight is dropped when it lands.
		void Clear()
		{
			m_Applied = m_Requested;
			m_Value.reset();
			m_Error.reset();
		}

	private:
		std::uint64_t m_Requested = 0;
		std::uint64_t m_Applied = 0;

		std::shared_ptr<const T> m_Value;
		std::optional<Core::Error> m_Error;
	};
}
