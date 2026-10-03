#include <doctest/doctest.h>

#include "Core/Base/Narrow.h"

#include <cstddef>
#include <cstdint>
#include <limits>

TEST_CASE("Narrow passes a value that fits through unchanged")
{
	CHECK(Core::Narrow<int>(std::size_t{42}) == 42);
	CHECK(Core::Narrow<int>(static_cast<std::size_t>(std::numeric_limits<int>::max())) ==
		  std::numeric_limits<int>::max());
	CHECK(Core::Narrow<std::uint16_t>(65535) == 65535);
	CHECK(Core::Narrow<int>(-5L) == -5);
}

TEST_CASE("NarrowFits tells a value that fits from one that does not")
{
	CHECK(Core::NarrowFits<int>(std::size_t{42}));
	CHECK_FALSE(Core::NarrowFits<int>(static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1));
	CHECK_FALSE(Core::NarrowFits<unsigned>(-1));
	CHECK_FALSE(Core::NarrowFits<std::uint8_t>(256));
}
