#pragma once

#include "Core/Base/Base.h"

#include <atomic>
#include <memory>

namespace Core {

	// Cancellation is cooperative: a job that never asks is never stopped. There is no way to
	// interrupt one, because killing a thread mid-work leaves whatever it was holding half-written.
	class CancellationToken
	{
	public:
		void Cancel() const { m_Cancelled->store(true, std::memory_order_relaxed); }

		bool IsCancelled() const { return m_Cancelled->load(std::memory_order_relaxed); }

	private:
		// relaxed is enough: this flag guards no other data. Everything a cancelled job hands
		// back crosses threads through its own mutex.
		Ref<std::atomic_bool> m_Cancelled = CreateRef<std::atomic_bool>(false);
	};
}
