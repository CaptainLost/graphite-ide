#include <doctest/doctest.h>

#include "UI/InfoBars/SettledBar.h"

#include <optional>
#include <string>

namespace {
	constexpr bool kSettled = true;
	constexpr bool kMidClick = false;
}

TEST_CASE("A bar appears or goes only once the pointer is settled")
{
	Graphite::SettledBar<std::string> bar;

	CHECK_FALSE(bar.Update(std::string{"No C++ language support"}, kMidClick).has_value());
	CHECK(bar.Update(std::string{"No C++ language support"}, kSettled).has_value());

	CHECK(bar.Update(std::nullopt, kMidClick).has_value());
	CHECK_FALSE(bar.Update(std::nullopt, kSettled).has_value());
}

TEST_CASE("What a shown bar says changes at once, settled or not")
{
	Graphite::SettledBar<std::string> bar;
	REQUIRE(bar.Update(std::string{"installing"}, kSettled).has_value());

	const std::optional<std::string>& shown = bar.Update(std::string{"installed"}, kMidClick);

	REQUIRE(shown.has_value());
	CHECK(*shown == "installed");
}
