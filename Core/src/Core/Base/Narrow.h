#pragma once

#include "Core/Base/Assert.h"

#include <concepts>
#include <utility>

namespace Core {

	template<std::integral To, std::integral From>
	constexpr bool NarrowFits(From value)
	{
		return std::in_range<To>(value);
	}

	// Asserted, not returned as an error: a value out of range here is a bug in the caller, not
	// something the environment can cause.
	template<std::integral To, std::integral From>
	constexpr To Narrow(From value)
	{
		CORE_ASSERT(NarrowFits<To>(value), "Narrow: the value does not fit the target type");
		return static_cast<To>(value);
	}
}
