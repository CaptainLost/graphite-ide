#include <doctest/doctest.h>

#include "Core/FileSystem/ChangeBatcher.h"

#include <chrono>
#include <filesystem>
#include <thread>

using Core::ChangeBatch;
using Core::ChangeBatcher;

namespace {

	// Long enough to pass any quiet period these tests use, short enough to keep them quick.
	constexpr std::chrono::milliseconds kQuietPeriod{20};

	std::filesystem::path Root()
	{
		return std::filesystem::temp_directory_path() / "change-batcher-root";
	}

	ChangeBatch TakeAfterQuietPeriod(ChangeBatcher& batcher)
	{
		std::this_thread::sleep_for(kQuietPeriod * 3);
		return batcher.TakeSettled(kQuietPeriod);
	}
}

TEST_CASE("A changed file reports the directory that holds it")
{
	ChangeBatcher batcher{Root()};
	batcher.Record(Root() / "src" / "main.cpp");

	const ChangeBatch batch = TakeAfterQuietPeriod(batcher);

	REQUIRE(batch.Directories.size() == 1);
	CHECK(batch.Directories.front() == (Root() / "src").make_preferred());
	CHECK_FALSE(batch.RepositoryStateChanged);
	CHECK_FALSE(batch.IgnoreRulesChanged);
}

TEST_CASE("A changed rules file says the ignore rules changed")
{
	ChangeBatcher batcher{Root()};
	batcher.Record(Root() / "sub" / ".gitignore");

	CHECK(TakeAfterQuietPeriod(batcher).IgnoreRulesChanged);
}

TEST_CASE("A moved HEAD says the repository's state changed and names no directory")
{
	ChangeBatcher batcher{Root()};
	batcher.Record(Root() / ".git" / "HEAD");

	const ChangeBatch batch = TakeAfterQuietPeriod(batcher);

	CHECK(batch.RepositoryStateChanged);
	CHECK(batch.Directories.empty());
}

TEST_CASE("A change among git's objects reports nothing")
{
	ChangeBatcher batcher{Root()};
	batcher.Record(Root() / ".git" / "objects" / "ab" / "cdef");

	const ChangeBatch batch = TakeAfterQuietPeriod(batcher);

	CHECK(batch.Directories.empty());
	CHECK_FALSE(batch.RepositoryStateChanged);
}

TEST_CASE("An overflow says so")
{
	ChangeBatcher batcher{Root()};
	batcher.RecordOverflow();

	CHECK(TakeAfterQuietPeriod(batcher).Overflowed);
}

TEST_CASE("Nothing is handed over before the quiet period has passed")
{
	ChangeBatcher batcher{Root()};
	batcher.Record(Root() / "a.txt");

	CHECK(batcher.TakeSettled(std::chrono::hours{1}).Directories.empty());
	CHECK(batcher.SettlesAt(kQuietPeriod).has_value());
}
