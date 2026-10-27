#pragma once

#include "Core/Jobs/CancellationToken.h"

#include <cstddef>
#include <utility>

namespace Core {

	// WARNING: Asks at the first step, then once enough steps or bytes have passed, so a few long steps are as prompt as many short ones.
	class CancellationPacing
	{
	public:
		explicit CancellationPacing(CancellationToken cancellation) : m_Cancellation(std::move(cancellation)) {}

		bool CancelledBefore(std::size_t stepBytes)
		{
			const bool askedBefore = m_Asked;
			const bool enoughWorkSinceAsked = m_StepsSinceAsked >= kStepsBetweenAsks || m_BytesSinceAsked >= kBytesBetweenAsks;

			m_StepsSinceAsked += 1;
			m_BytesSinceAsked += stepBytes;

			if (askedBefore && !enoughWorkSinceAsked)
			{
				return false;
			}

			m_Asked = true;
			m_StepsSinceAsked = 1;
			m_BytesSinceAsked = stepBytes;

			return m_Cancellation.IsCancelled();
		}

	private:
		static constexpr std::size_t kStepsBetweenAsks = 16384;
		static constexpr std::size_t kBytesBetweenAsks = 1024 * 1024;

		CancellationToken m_Cancellation;
		bool m_Asked = false;
		std::size_t m_StepsSinceAsked = 0;
		std::size_t m_BytesSinceAsked = 0;
	};
}
