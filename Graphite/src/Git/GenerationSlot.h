#pragma once

#include "Core/Base/Base.h"

#include "Core/Base/Result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace Graphite {

	enum class SlotLanding
	{
		Stale,
		Failed,
		Applied
	};

	// WARNING: UI thread only.
	template<typename T>
	class GenerationSlot
	{
	public:
		std::uint64_t Begin() { return ++m_Requested; }

		// WARNING: A failure keeps the last good snapshot; a panel that blanks itself because one
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
			m_Value = Core::CreateRef<const T>(std::move(result).GetValue());
			return SlotLanding::Applied;
		}

		SlotLanding LandSameAsHeld(std::uint64_t generation)
		{
			CORE_ASSERT(m_Value != nullptr, "only an answer equal to a held snapshot can keep it");

			if (generation <= m_Applied)
			{
				return SlotLanding::Stale;
			}

			m_Applied = generation;
			m_Error.reset();
			return SlotLanding::Applied;
		}

		Core::Ref<const T> Get() const { return m_Value; }

		const std::optional<Core::Error>& GetError() const { return m_Error; }

		bool IsInFlight() const { return m_Requested != m_Applied; }

		std::uint64_t GetRequested() const { return m_Requested; }
		std::uint64_t GetApplied() const { return m_Applied; }

		// WARNING: The generations move forward rather than resetting, so an answer still in
		// flight is dropped when it lands.
		void Clear()
		{
			m_Applied = m_Requested;
			m_Value.reset();
			m_Error.reset();
		}

	private:
		std::uint64_t m_Requested = 0;
		std::uint64_t m_Applied = 0;

		Core::Ref<const T> m_Value;
		std::optional<Core::Error> m_Error;
	};
}
