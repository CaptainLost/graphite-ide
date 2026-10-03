#pragma once

#include "Core/Base/Assert.h"

#include <concepts>
#include <utility>

namespace Core {

	// Whether `value` survives a conversion to `To` unchanged - neither cut off nor turned
	// from negative to positive or back.
	template<std::integral To, std::integral From>
	constexpr bool NarrowFits(From value)
	{
		return std::in_range<To>(value);
	}

	// A narrowing conversion that says it is one, and asserts that nothing is lost: a size
	// handed to an API that counts in int, say. Asserted rather than thrown (ADR 0004): a
	// value out of range here is a mistake in the caller, not something the environment does.
	template<std::integral To, std::integral From>
	constexpr To Narrow(From value)
	{
		CORE_ASSERT(NarrowFits<To>(value), "Narrow: the value does not fit the target type");
		return static_cast<To>(value);
	}
}
