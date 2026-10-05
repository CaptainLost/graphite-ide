#include <doctest/doctest.h>

#include "Core/Platform/IconResource.h"

TEST_CASE("LoadIconResource is empty for an icon the executable does not have")
{
	CHECK(Core::LoadIconResource("NO_SUCH_ICON", 16).Empty());
}

TEST_CASE("LoadIconResource is empty for a size that is not positive")
{
	CHECK(Core::LoadIconResource("NO_SUCH_ICON", 0).Empty());
	CHECK(Core::LoadIconResource("NO_SUCH_ICON", -16).Empty());
}

TEST_CASE("IconResourceSizes is empty for an icon the executable does not have")
{
	CHECK(Core::IconResourceSizes("NO_SUCH_ICON").empty());
}
