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
	const int largestUint16 = std::numeric_limits<std::uint16_t>::max();
	CHECK(Core::Narrow<std::uint16_t>(largestUint16) == largestUint16);
	CHECK(Core::Narrow<int>(-5L) == -5);
}

TEST_CASE("NarrowFits tells a value that fits from one that would be cut off or change sign")
{
	CHECK(Core::NarrowFits<int>(std::size_t{42}));
	CHECK_FALSE(Core::NarrowFits<int>(static_cast<std::size_t>(std::numeric_limits<int>::max()) + 1));
	CHECK_FALSE(Core::NarrowFits<unsigned>(-1));
	const int oneAboveLargestUint8 = int{std::numeric_limits<std::uint8_t>::max()} + 1;
	CHECK_FALSE(Core::NarrowFits<std::uint8_t>(oneAboveLargestUint8));
}
