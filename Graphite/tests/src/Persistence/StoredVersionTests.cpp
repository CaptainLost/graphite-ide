#include <doctest/doctest.h>

#include "Persistence/StoredVersion.h"

#include <nlohmann/json.hpp>

#include <ostream>

namespace {
	constexpr std::int64_t kCurrent = 2;

	Graphite::StoredVersion VersionOf(const char* text)
	{
		return Graphite::StoredVersionOf(nlohmann::json::parse(text), "version", kCurrent);
	}
}

TEST_CASE("A stored version is measured against the one this build writes")
{
	CHECK(VersionOf(R"({"version": 1})") == Graphite::StoredVersion::Older);
	CHECK(VersionOf(R"({"version": 2})") == Graphite::StoredVersion::Current);
	CHECK(VersionOf(R"({"version": 3})") == Graphite::StoredVersion::Newer);
}

TEST_CASE("A stored version that is absent or not a number says so")
{
	CHECK(VersionOf(R"({"other": 2})") == Graphite::StoredVersion::Missing);
	CHECK(VersionOf(R"({"version": "2"})") == Graphite::StoredVersion::NotANumber);
	CHECK(VersionOf(R"({"version": null})") == Graphite::StoredVersion::NotANumber);
}

TEST_CASE("A stored version written as a fraction is still measured")
{
	CHECK(VersionOf(R"({"version": 2.5})") == Graphite::StoredVersion::Newer);
	CHECK(VersionOf(R"({"version": 1.5})") == Graphite::StoredVersion::Older);
}