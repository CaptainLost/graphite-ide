#include <doctest/doctest.h>

#include "Watching/TreeWatch.h"

#include "TempDirectory.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <thread>

namespace {

	std::unique_ptr<Agent::TreeWatch> StartOn(const std::filesystem::path& root)
	{
		Core::Result<std::unique_ptr<Agent::TreeWatch>> watch = Agent::TreeWatch::Start(root);
		REQUIRE(watch.HasValue());
		return std::move(watch.GetValue());
	}

	// Polls until a settled batch satisfies the check, or two seconds pass.
	bool Eventually(Agent::TreeWatch& watch, const std::function<bool(const Core::ChangeBatch&)>& check)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};

		while (std::chrono::steady_clock::now() < deadline)
		{
			watch.Poll();

			if (check(watch.TakeSettled()))
			{
				return true;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds{20});
		}

		return false;
	}

	bool Names(const Core::ChangeBatch& batch, const std::filesystem::path& directory)
	{
		return std::ranges::find(batch.Directories, directory) != batch.Directories.end();
	}
}

TEST_CASE("A file written in the root reports the root")
{
	TempDirectory directory;
	auto watch = StartOn(directory.GetPath());

	directory.MakeFile("a.txt", "x");

	CHECK(Eventually(*watch, [&](const Core::ChangeBatch& batch) { return Names(batch, directory.GetPath()); }));
}

TEST_CASE("A file written in a folder that was there before the watch reports that folder")
{
	TempDirectory directory;
	directory.MakeFile("sub/existing.txt", "x");
	auto watch = StartOn(directory.GetPath());

	directory.MakeFile("sub/new.txt", "y");

	CHECK(Eventually(*watch, [&](const Core::ChangeBatch& batch) { return Names(batch, directory.GetPath() / "sub"); }));
}

TEST_CASE("A file made at once inside a new folder is reported, though the folder was not watched yet")
{
	TempDirectory directory;
	auto watch = StartOn(directory.GetPath());

	std::filesystem::create_directory(directory.GetPath() / "fresh");
	std::ofstream{directory.GetPath() / "fresh" / "x.txt"} << "x";

	CHECK(Eventually(*watch, [&](const Core::ChangeBatch& batch) { return Names(batch, directory.GetPath() / "fresh"); }));

	// The new folder is watched from now on.
	directory.MakeFile("fresh/later.txt", "z");
	CHECK(Eventually(*watch, [&](const Core::ChangeBatch& batch) { return Names(batch, directory.GetPath() / "fresh"); }));
}

TEST_CASE("A rules file written says the ignore rules changed")
{
	TempDirectory directory;
	auto watch = StartOn(directory.GetPath());

	directory.MakeFile(".gitignore", "*.log\n");

	CHECK(Eventually(*watch, [](const Core::ChangeBatch& batch) { return batch.IgnoreRulesChanged; }));
}

TEST_CASE("A moved HEAD says the repository's state changed")
{
	TempDirectory directory;
	directory.MakeFile(".git/HEAD", "ref: refs/heads/main\n");
	auto watch = StartOn(directory.GetPath());

	directory.MakeFile(".git/HEAD", "ref: refs/heads/other\n");

	CHECK(Eventually(*watch, [](const Core::ChangeBatch& batch) { return batch.RepositoryStateChanged; }));
}

TEST_CASE("Watching a folder that is not there is not found")
{
	TempDirectory directory;

	const auto watch = Agent::TreeWatch::Start(directory.GetPath() / "missing");

	REQUIRE_FALSE(watch.HasValue());
	CHECK(watch.GetError().Code == Core::ErrorCode::NotFound);
}
