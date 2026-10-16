#include <doctest/doctest.h>

#include "Core/Git/Oid.h"

#include <optional>
#include <ostream>
#include <string>

namespace {
	constexpr const char* kFortyHexDigits = "0123456789abcdef0123456789abcdef01234567";
}

TEST_CASE("An id read from its forty hex digits writes back the same digits")
{
	const std::optional<Core::Oid> id = Core::Oid::FromHex(kFortyHexDigits);

	REQUIRE(id.has_value());
	CHECK(id->ToHex() == kFortyHexDigits);
}

TEST_CASE("An id reads upper-case digits as their lower-case selves")
{
	const std::optional<Core::Oid> id = Core::Oid::FromHex("0123456789ABCDEF0123456789ABCDEF01234567");

	REQUIRE(id.has_value());
	CHECK(id->ToHex() == kFortyHexDigits);
}

TEST_CASE("Text that is not forty hex digits is no id")
{
	CHECK_FALSE(Core::Oid::FromHex("").has_value());
	CHECK_FALSE(Core::Oid::FromHex("0123456").has_value());
	CHECK_FALSE(Core::Oid::FromHex(std::string{kFortyHexDigits} + "89").has_value());
	CHECK_FALSE(Core::Oid::FromHex("g123456789abcdef0123456789abcdef01234567").has_value());
}