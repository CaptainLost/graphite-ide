#include <doctest/doctest.h>

#include "Core/Base/ByteSize.h"

TEST_CASE("FormatByteSize for a size read in passing reads as bytes below a kilobyte, then one decimal of the largest unit")
{
	CHECK(Core::FormatByteSize(812) == "812 B");
	CHECK(Core::FormatByteSize(3277) == "3.2 KB");
	CHECK(Core::FormatByteSize(1468006) == "1.4 MB");
}

TEST_CASE("The whole-unit formats for a limit compared at a glance round down and name the unit")
{
	CHECK(Core::FormatWholeKilobytes(3277) == "3 KB");
	CHECK(Core::FormatWholeMegabytes(35651584) == "34 MB");
	CHECK(Core::FormatWholeMegabytes(1048575) == "0 MB");
}
