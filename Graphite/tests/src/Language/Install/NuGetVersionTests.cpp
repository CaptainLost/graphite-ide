#include <doctest/doctest.h>

#include "Language/Install/NuGetVersion.h"

#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace {

	Graphite::NuGetVersion VersionOf(std::string_view text)
	{
		const std::optional<Graphite::NuGetVersion> version = Graphite::NuGetVersion::Parse(text);
		REQUIRE(version.has_value());
		return *version;
	}
}

TEST_CASE("Prerelease builds of one release order by their numbers and below the release")
{
	CHECK(VersionOf("5.12.0-1.26426.8") < VersionOf("5.12.0-1.26475.2"));
	CHECK(VersionOf("5.11.0") < VersionOf("5.12.0-1.26426.8"));
	CHECK(VersionOf("5.12.0-1.26475.2") < VersionOf("5.12.0"));
	CHECK(VersionOf("1.0.0-rc.2") < VersionOf("1.0.0-rc.10"));
}

TEST_CASE("A numeric label sorts below a word and a shorter label list below a longer one")
{
	CHECK(VersionOf("1.0.0-1") < VersionOf("1.0.0-alpha"));
	CHECK(VersionOf("1.0.0-alpha") < VersionOf("1.0.0-alpha.1"));
	CHECK(VersionOf("1.0.1-alpha10") < VersionOf("1.0.1-alpha2"));
	CHECK(VersionOf("1.0.0-alpha.beta") < VersionOf("1.0.0-beta"));
}

TEST_CASE("Case and build metadata and missing numbers do not change a version")
{
	CHECK(VersionOf("1.0.0-Alpha") == VersionOf("1.0.0-alpha"));
	CHECK(VersionOf("1.0.7+r3456") == VersionOf("1.0.7"));
	CHECK(VersionOf("1") == VersionOf("1.0.0.0"));
	CHECK(VersionOf("1.01.1") == VersionOf("1.1.1"));
	CHECK(VersionOf("1.0.0-rc.010") == VersionOf("1.0.0-rc.10"));
	CHECK(VersionOf("1.0.0") < VersionOf("1.0.0.1"));
}

TEST_CASE("Only a version with a label is a prerelease")
{
	CHECK(VersionOf("5.12.0-1.26475.2").IsPrerelease());
	CHECK_FALSE(VersionOf("5.12.0").IsPrerelease());
	CHECK_FALSE(VersionOf("1.0.7+r3456").IsPrerelease());
}

TEST_CASE("Text that is not a version parses to nothing")
{
	for (const std::string_view text :
		 {"", "v1.0", "1..0", "1.0.0-", "1.0.0-a..b", "1.2.3.4.5", "1.0.0-a_b", "abc", "99999999999999999999.0"})
	{
		INFO(std::string{text});
		CHECK_FALSE(Graphite::NuGetVersion::Parse(text).has_value());
	}
}
