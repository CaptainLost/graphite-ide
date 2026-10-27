#pragma once

#include "Core/Base/Base.h"

#include <atomic>
#include <cstddef>
#include <memory>

namespace Core {

	// Cancellation is cooperative: a job that never asks is never stopped. There is no way to
	// interrupt one, because killing a thread mid-work leaves whatever it was holding half-written.
	class CancellationToken
	{
	public:
		// WARNING: For tests that cancel partway through: it reads as cancelled from its checkNumber-th ask on.
		static CancellationToken CancelledAtCheck(std::size_t checkNumber)
		{
			CancellationToken token;
			token.m_AsksBeforeCancel = CreateRef<std::atomic<std::size_t>>(checkNumber);

			return token;
		}

		void Cancel() const { m_Cancelled->store(true, std::memory_order_relaxed); }

		bool IsCancelled() const
		{
			const bool countsAsks = m_AsksBeforeCancel != nullptr;

			if (countsAsks && m_AsksBeforeCancel->fetch_sub(1, std::memory_order_relaxed) <= 1)
			{
				Cancel();
			}

			return m_Cancelled->load(std::memory_order_relaxed);
		}

	private:
		// relaxed is enough: this flag guards no other data. Everything a cancelled job hands
		// back crosses threads through its own mutex.
		Ref<std::atomic_bool> m_Cancelled = CreateRef<std::atomic_bool>(false);

		Ref<std::atomic<std::size_t>> m_AsksBeforeCancel;
	};
}
