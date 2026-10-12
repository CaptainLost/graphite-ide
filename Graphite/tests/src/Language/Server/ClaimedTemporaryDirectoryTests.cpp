#include <doctest/doctest.h>

#include "Language/Server/ClaimedTemporaryDirectory.h"

#include "TempDirectory.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <utility>

TEST_CASE("A claimed directory lies under the directory it was claimed in and outlives a sweep of it")
{
	TempDirectory temp;
	const std::filesystem::path parent = temp.GetPath() / "server-temp";

	Core::Result<Graphite::ClaimedTemporaryDirectory> claimed = Graphite::ClaimedTemporaryDirectory::Claim(parent);
	REQUIRE(claimed);
	const std::filesystem::path claimedPath = claimed.GetValue().GetPath();

	std::ofstream{claimedPath / "preamble-1.pch"} << "in use";

	Graphite::SweepUnclaimedTemporaryDirectories(parent);

	CHECK(claimedPath.parent_path() == parent);
	CHECK(std::filesystem::is_directory(claimedPath));
	CHECK(std::filesystem::exists(claimedPath / "preamble-1.pch"));
}

TEST_CASE("Two claims in one directory get a directory each")
{
	TempDirectory temp;
	const std::filesystem::path parent = temp.GetPath() / "server-temp";

	Core::Result<Graphite::ClaimedTemporaryDirectory> first = Graphite::ClaimedTemporaryDirectory::Claim(parent);
	Core::Result<Graphite::ClaimedTemporaryDirectory> second = Graphite::ClaimedTemporaryDirectory::Claim(parent);
	REQUIRE(first);
	REQUIRE(second);

	CHECK(first.GetValue().GetPath() != second.GetValue().GetPath());
}

TEST_CASE("A sweep removes a directory nobody claims along with what a killed server left in it")
{
	TempDirectory temp;
	const std::filesystem::path parent = temp.MakeDirectory("server-temp");
	const std::filesystem::path leftover = temp.MakeFile("server-temp/7/preamble-0d27db.pch", "35 MB once");

	Graphite::SweepUnclaimedTemporaryDirectories(parent);

	CHECK_FALSE(std::filesystem::exists(leftover.parent_path()));
	CHECK(std::filesystem::is_empty(parent));
}

TEST_CASE("The directory of a claim that has ended is removed by the next sweep")
{
	TempDirectory temp;
	const std::filesystem::path parent = temp.GetPath() / "server-temp";
	std::filesystem::path endedPath;

	{
		Core::Result<Graphite::ClaimedTemporaryDirectory> claimed = Graphite::ClaimedTemporaryDirectory::Claim(parent);
		REQUIRE(claimed);
		endedPath = claimed.GetValue().GetPath();
	}

	Graphite::SweepUnclaimedTemporaryDirectories(parent);

	CHECK_FALSE(std::filesystem::exists(endedPath));
}

TEST_CASE("A claim moved to another owner stays claimed")
{
	TempDirectory temp;
	const std::filesystem::path parent = temp.GetPath() / "server-temp";

	Core::Result<Graphite::ClaimedTemporaryDirectory> claimed = Graphite::ClaimedTemporaryDirectory::Claim(parent);
	REQUIRE(claimed);
	std::optional<Graphite::ClaimedTemporaryDirectory> owner;
	owner.emplace(std::move(claimed.GetValue()));

	Graphite::SweepUnclaimedTemporaryDirectories(parent);

	CHECK(std::filesystem::is_directory(owner->GetPath()));
}
